#pragma once
#include <VHAL.h>
#include <atomic>
#include <cstddef>
#include <type_traits>

// How a ring or a packet queue is accessed:
//   locked        any number of writers; every access takes a critical section
//   singleWriter  one producer context writes, one consumer context reads; no critical section. The indexes are published with
//                 release stores and read with acquire loads, so the data of an element is visible ahead of its index.
//   callerLocked  the caller serializes every access; no critical section and no barrier
enum class RingAccess : uint8 {
	locked,
	singleWriter,
	callerLocked
};


// The head and the tail of a singleWriter ring. The producer writes head and the consumer writes tail, and each reads the other with an
// acquire load. The entries are the slots from the tail up to the head, and one slot stays empty.
class RingIndexes {
public:
	std::atomic<uint16> head;
	std::atomic<uint16> tail;


	constexpr RingIndexes()
		: head(0)
		, tail(0) {
	}


	constexpr RingIndexes(const RingIndexes& other)
		: head(other.head.load(std::memory_order_relaxed))
		, tail(other.tail.load(std::memory_order_relaxed)) {
	}


	RingIndexes& operator=(const RingIndexes& other) {
		head.store(other.head.load(std::memory_order_relaxed), std::memory_order_relaxed);
		tail.store(other.tail.load(std::memory_order_relaxed), std::memory_order_relaxed);
		return *this;
	}


	void Reset() {
		head.store(0, std::memory_order_relaxed);
		tail.store(0, std::memory_order_relaxed);
	}
};


// The indexes of a locked or callerLocked ring: the slot of the oldest element and the number of elements. A critical section, or the
// caller, guards both.
struct LockedCursor {
	uint16 readIndex = 0;
	uint16 size = 0;


	void Reset() {
		readIndex = 0;
		size = 0;
	}
};


// The indexes of a ring or a packet queue of a capacity, and the marks of its transactional reads. Concurrent is true for singleWriter,
// which keeps RingIndexes and one slot more than its capacity; the other modes keep LockedCursor and exactly the capacity in slots. The
// capacity is an argument of each call, so a class of this template holds no element type.
template <bool Concurrent>
class RingCursor {
public:
	// The slots that hold a capacity of elements.
	static constexpr uint16 SlotCount(uint16 capacity) {
		return Concurrent ? static_cast<uint16>(capacity + 1) : capacity;
	}


	// The slot places after index, in a ring of slots slots. index is below slots and places is at most slots, so one subtraction wraps the
	// sum. No division is used: a core without a divide instruction would link the library's division routine for it.
	static uint16 Advance(uint16 index, uint32 places, uint16 slots) {
		uint32 slot = static_cast<uint32>(index) + places;
		if (slot >= slots) {
			slot -= slots;
		}

		return static_cast<uint16>(slot);
	}


	// The count of elements held. A snapshot: both indexes are read.
	uint16 Size([[maybe_unused]] uint16 capacity) const {
		if constexpr (Concurrent) {
			return Held(indexes.head.load(std::memory_order_acquire), indexes.tail.load(std::memory_order_acquire), SlotCount(capacity));
		} else {
			return indexes.size;
		}
	}


	// Producer: the free elements, and the slot where the next element goes. Each index is read once: the producer's own head plainly,
	// the consumer's tail with an acquire load, so the slot is free before the caller writes it.
	_AInline uint16 Room(uint16 capacity, uint16& writeSlot) const {
		if constexpr (Concurrent) {
			writeSlot = indexes.head.load(std::memory_order_relaxed);
			uint16 used = Held(writeSlot, indexes.tail.load(std::memory_order_acquire), SlotCount(capacity));
			return static_cast<uint16>(capacity - used);
		} else {
			uint16 used = indexes.size;
			writeSlot = Advance(indexes.readIndex, used, capacity);
			return static_cast<uint16>(capacity - used);
		}
	}


	// Consumer: the held elements, and the slot of the oldest one. Each index is read once: the consumer's own tail plainly, the
	// producer's head with an acquire load, so the data behind the head is visible.
	_AInline uint16 Available([[maybe_unused]] uint16 capacity, uint16& readSlot) const {
		if constexpr (Concurrent) {
			readSlot = indexes.tail.load(std::memory_order_relaxed);
			return Held(indexes.head.load(std::memory_order_acquire), readSlot, SlotCount(capacity));
		} else {
			readSlot = indexes.readIndex;
			return indexes.size;
		}
	}


	// The slot of the oldest element.
	uint16 ReadSlot() const {
		if constexpr (Concurrent) {
			return indexes.tail.load(std::memory_order_relaxed);
		} else {
			return indexes.readIndex;
		}
	}


	// Publishes count elements written from writeSlot, which Room returned.
	_AInline void Produce(uint16 capacity, [[maybe_unused]] uint16 writeSlot, uint16 count) {
		if constexpr (Concurrent) {
			indexes.head.store(Advance(writeSlot, count, SlotCount(capacity)), std::memory_order_release);
		} else {
			indexes.size = static_cast<uint16>(indexes.size + count);
		}
	}


	// Releases count elements from readSlot, which Available returned. The data of them must be read first.
	_AInline void Consume(uint16 capacity, uint16 readSlot, uint16 count) {
		if constexpr (Concurrent) {
			indexes.tail.store(Advance(readSlot, count, SlotCount(capacity)), std::memory_order_release);
		} else {
			indexes.readIndex = Advance(readSlot, count, capacity);
			indexes.size = static_cast<uint16>(indexes.size - count);
		}
	}


	// Removes up to count of the oldest elements and returns how many. Nothing is removed while marked reads are uncommitted.
	uint16 Drop(uint16 capacity, uint16 count) {
		if (uncommittedReadCount > 0) {
			return 0;
		}

		uint16 readSlot = 0;
		uint16 available = Available(capacity, readSlot);
		uint16 dropped = count < available ? count : available;
		Consume(capacity, readSlot, dropped);

		return dropped;
	}


	// Marks count elements as read. The marks are not removed until CommitReads, and are refused when they exceed the held elements.
	bool MarkRead(uint16 capacity, uint16 count) {
		if (count > Size(capacity) - uncommittedReadCount) {
			return false;
		}

		uncommittedReadCount = static_cast<uint16>(uncommittedReadCount + count);
		return true;
	}


	// Removes the marked elements.
	void CommitReads(uint16 capacity) {
		Consume(capacity, ReadSlot(), uncommittedReadCount);
		uncommittedReadCount = 0;
	}


	void RollbackReads() {
		uncommittedReadCount = 0;
	}


	uint16 Uncommitted() const {
		return uncommittedReadCount;
	}


	// Sets the count of a locked cursor. The producer owns the count of a singleWriter cursor, so it is not set there.
	void SetCount(uint16 count) {
		static_assert(!Concurrent, "RingCursor::SetCount writes the count, which the producer owns in singleWriter mode");
		indexes.size = count;
	}


	void Reset() {
		indexes.Reset();
		uncommittedReadCount = 0;
	}


private:
	using Indexes = std::conditional_t<Concurrent, RingIndexes, LockedCursor>;

	Indexes indexes;
	uint16 uncommittedReadCount = 0;


	// The entries from tail up to head, in a ring of slots slots. Both indexes are below slots, so one subtraction wraps the difference.
	static uint16 Held(uint16 head, uint16 tail, uint16 slots) {
		uint32 held = static_cast<uint32>(head) + slots - tail;
		if (held >= slots) {
			held -= slots;
		}

		return static_cast<uint16>(held);
	}
};
