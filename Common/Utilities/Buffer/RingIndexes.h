#pragma once
#include <VHAL.h>
#include <atomic>
#include <cstddef>

// How a ring or a packet queue is accessed:
//   locked        any number of writers; every access takes a critical section
//   singleWriter  one producer context writes, one consumer context reads; no critical section. The indexes are published with
//                 release stores and read with acquire loads, so the data of an element is visible before its index.
//   callerLocked  the caller serializes every access; no critical section and no barrier
enum class RingAccess : uint8 {
	locked,
	singleWriter,
	callerLocked
};


// The head and the tail of a ring of slotCount slots. The entries are the slots from the tail up to the head, and one slot always
// stays empty: a ring of slotCount slots holds slotCount - 1 entries. The head is written by the producer, the tail by the consumer.
// The functions are plain arithmetic on the indexes and the slot count, so every ring type of an image shares one copy of them.
class RingIndexes {
public:
	std::atomic<uint16> head;
	std::atomic<uint16> tail;


	RingIndexes()
		: head(0)
		, tail(0) {
	}


	RingIndexes(const RingIndexes& other)
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


	// The slot after index.
	static uint16 Next(uint16 index, uint16 slotCount) {
		return Advance(index, 1, slotCount);
	}


	// The slot places after index, for places below slotCount.
	static uint16 Advance(uint16 index, uint32 places, uint16 slotCount) {
		return static_cast<uint16>((static_cast<uint32>(index) + places) % slotCount);
	}


	// The entries from tail up to head.
	static uint16 Held(uint16 head, uint16 tail, uint16 slotCount) {
		return static_cast<uint16>((static_cast<uint32>(head) + slotCount - tail) % slotCount);
	}
};


// The indexes of a locked ring: its read index and its element count. The critical section guards both.
struct LockedCursor {
	uint16 readIndex = 0;
	uint16 size = 0;


	void Reset() {
		readIndex = 0;
		size = 0;
	}
};
