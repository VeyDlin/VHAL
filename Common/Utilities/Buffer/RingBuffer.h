#pragma once
#include <VHAL.h>
#include <Utilities/Buffer/RingIndexes.h>
#include <Utilities/Memory/Storage.h>
#include <algorithm>
#include <concepts>
#include <limits>
#include <span>
#include <utility>


// A ring of BufferSize elements in one of the access modes of RingAccess. Policy says where the elements are kept (Memory::Inside,
// Memory::FromHeap or Memory::External). A ring holds BufferSize elements in every mode.
//
// The locked and callerLocked modes keep a read index and a count, so their storage holds BufferSize elements. The singleWriter mode
// keeps head and tail indexes (RingIndexes), which leave one slot empty: its storage holds BufferSize + 1 elements, and
// Memory::External needs that many. The singleWriter mode is for one producer context, which calls Push, and one consumer context, which
// calls every other member; it takes no critical section. The callerLocked mode takes no critical section either, and the caller
// serializes every access.
//
// The pointers from Front() and Peek(index) stay valid until the element is popped or dropped. In locked mode the caller makes sure no
// other context pops meanwhile.
template <typename ElementType, uint16 BufferSize, typename Policy = Memory::Inside, RingAccess Access = RingAccess::locked>
class RingBuffer {
	static_assert(BufferSize > 0, "RingBuffer with size 0 are forbidden");
	static_assert(
		Access != RingAccess::singleWriter || BufferSize < std::numeric_limits<uint16>::max(),
		"A singleWriter RingBuffer keeps N + 1 indexes: N must be below 65535");


private:
	using Cursor = RingCursor<Access == RingAccess::singleWriter>;

	static constexpr bool isLocked = Access == RingAccess::locked;
	static constexpr uint16 slotCount = Cursor::SlotCount(BufferSize);

	typename Policy::template Storage<ElementType, slotCount> storage;
	Cursor cursor;


	static void EnterSection() {
		if constexpr (isLocked) {
			System::CriticalSection(true);
		}
	}


	static void LeaveSection() {
		if constexpr (isLocked) {
			System::CriticalSection(false);
		}
	}


public:
	// The storage is value-initialized here and only here, so a static ring is constant-initialized and needs no start-up code.
	constexpr RingBuffer()
		: storage() {
	}


	RingBuffer(const RingBuffer&) = default;
	RingBuffer& operator=(const RingBuffer&) = default;


	// A move takes the elements with the storage. The source is left empty, so its indexes and marks are reset with them.
	RingBuffer(RingBuffer&& other) noexcept
		: storage(std::move(other.storage))
		, cursor(other.cursor) {
		other.cursor.Reset();
	}


	RingBuffer& operator=(RingBuffer&& other) noexcept {
		if (this != &other) {
			storage = std::move(other.storage);
			cursor = other.cursor;
			other.cursor.Reset();
		}

		return *this;
	}


	// The arguments go to the policy's storage, for example the memory of Memory::External.
	template <typename... Arguments>
		requires(sizeof...(Arguments) > 0 && std::constructible_from<typename Policy::template Storage<ElementType, slotCount>, Arguments...>)
	explicit RingBuffer(Arguments&&... arguments)
		: storage(std::forward<Arguments>(arguments)...) {
	}


	// Producer: pushes one element at the back.
	ResultStatus Push(const ElementType inElement) {
		if (!storage.Ready()) {
			return ResultStatus::notReady;
		}

		EnterSection();
		uint16 writeSlot = 0;
		if (cursor.Room(BufferSize, writeSlot) == 0) {
			LeaveSection();
			return ResultStatus::filled;
		}

		storage.Data()[writeSlot] = inElement;
		cursor.Produce(BufferSize, writeSlot, 1);
		LeaveSection();

		return ResultStatus::ok;
	}


	// Producer: pushes the element that the pointer points to. A null pointer is an error.
	ResultStatus Push(const ElementType* const inElement) {
		if (!inElement) {
			return ResultStatus::error;
		}

		return Push(*inElement);
	}


	// Producer: pushes as many of elements as fit and returns how many were pushed. The whole copy runs in one critical section, where a
	// Push() per element would take one each. The copy is at most two segments: up to the end of the buffer and from its start.
	uint16 Push(std::span<const ElementType> elements) {
		if (!storage.Ready()) {
			return 0;
		}

		EnterSection();

		uint16 writeSlot = 0;
		uint32 count = static_cast<uint32>(elements.size());
		uint32 freeSpace = cursor.Room(BufferSize, writeSlot);
		if (count > freeSpace) {
			count = freeSpace;
		}

		uint32 writeIndex = writeSlot;
		uint32 firstSegment = slotCount - writeIndex;
		if (firstSegment > count) {
			firstSegment = count;
		}

		ElementType* items = storage.Data();
		std::copy_n(elements.data(), firstSegment, &items[writeIndex]);
		if (count > firstSegment) {
			std::copy_n(elements.data() + firstSegment, count - firstSegment, items);
		}
		cursor.Produce(BufferSize, writeSlot, static_cast<uint16>(count));
		LeaveSection();

		return static_cast<uint16>(count);
	}


	// Consumer: pops the oldest element.
	Result<ElementType> Pop() {
		EnterSection();
		// Marked elements must stay in place until CommitReads or RollbackReads
		if (cursor.Uncommitted() > 0) {
			LeaveSection();
			return ResultStatus::busy;
		}

		uint16 readSlot = 0;
		if (cursor.Available(BufferSize, readSlot) == 0) {
			LeaveSection();
			return ResultStatus::empty;
		}

		ElementType data = storage.Data()[readSlot];
		cursor.Consume(BufferSize, readSlot, 1);
		LeaveSection();

		return data;
	}


	// Consumer: pops up to out.size() elements into out and returns how many were popped. Like Pop(), it refuses with 0 while marked reads
	// are uncommitted. The copy runs in one critical section, in at most two segments, so keep out to what the caller can afford
	// to copy with interrupts masked.
	uint16 Pop(std::span<ElementType> out) {
		EnterSection();
		// Marked elements must stay in place until CommitReads or RollbackReads
		if (cursor.Uncommitted() > 0) {
			LeaveSection();
			return 0;
		}

		uint16 readSlot = 0;
		uint32 count = static_cast<uint32>(out.size());
		uint32 held = cursor.Available(BufferSize, readSlot);
		if (count > held) {
			count = held;
		}

		uint32 firstSegment = slotCount - readSlot;
		if (firstSegment > count) {
			firstSegment = count;
		}

		ElementType* items = storage.Data();
		std::copy_n(&items[readSlot], firstSegment, out.data());
		std::copy_n(items, count - firstSegment, out.data() + firstSegment);
		cursor.Consume(BufferSize, readSlot, static_cast<uint16>(count));
		LeaveSection();

		return static_cast<uint16>(count);
	}


	// Consumer: removes up to count of the oldest elements without copying them, and returns how many were removed. Like Pop(), it
	// removes nothing while marked reads are uncommitted.
	uint16 Drop(uint16 count) {
		EnterSection();
		uint16 dropped = cursor.Drop(BufferSize, count);
		LeaveSection();

		return dropped;
	}


	// Consumer: the oldest element, in place, or nullptr when the ring is empty. Marked elements are included.
	const ElementType* Front() const {
		EnterSection();
		uint16 readSlot = 0;
		if (cursor.Available(BufferSize, readSlot) == 0) {
			LeaveSection();
			return nullptr;
		}

		const ElementType* item = &storage.Data()[readSlot];
		LeaveSection();

		return item;
	}


	// Consumer: the element at index (0 is the oldest), in place, or nullptr when index is not held.
	const ElementType* Peek(uint16 index) const {
		EnterSection();
		uint16 readSlot = 0;
		if (index >= cursor.Available(BufferSize, readSlot)) {
			LeaveSection();
			return nullptr;
		}

		const ElementType* item = &storage.Data()[Cursor::Advance(readSlot, index, slotCount)];
		LeaveSection();

		return item;
	}


	bool IsFull() const {
		return cursor.Size(BufferSize) == BufferSize;
	}


	bool IsEmpty() const {
		return cursor.Size(BufferSize) == 0;
	}


	// Empties the ring and drops its marks too. It is the reset path: a reset must not keep the bytes of the old session.
	void Clear() {
		static_assert(
			Access != RingAccess::singleWriter,
			"RingBuffer::Clear is not offered in singleWriter mode: the consumer empties the ring with Drop(Size())");

		EnterSection();
		cursor.Reset();
		LeaveSection();
	}


	uint16 Size() const {
		return cursor.Size(BufferSize);
	}


	constexpr uint16 MaxSize() const {
		return BufferSize;
	}


	uint16 GetFreeSpace() const {
		return static_cast<uint16>(BufferSize - cursor.Size(BufferSize));
	}


	Result<ElementType> Peek() const {
		uint16 readSlot = 0;
		if (cursor.Available(BufferSize, readSlot) == 0) {
			return ResultStatus::empty;
		}
		return storage.Data()[readSlot];
	}


	ElementType operator[](uint16 inIndex) const {
		EnterSection();
		uint16 readSlot = 0;
		uint16 held = cursor.Available(BufferSize, readSlot);
		SystemAssert(inIndex < held);

		ElementType data = storage.Data()[Cursor::Advance(readSlot, inIndex, slotCount)];
		LeaveSection();

		return data;
	}


	// Peek at multiple elements without removing them
	// Returns actual number of elements peeked (may be less than requested)
	uint16 PeekMultiple(ElementType* outBuffer, uint16 count, uint16 offset = 0) const {
		EnterSection();

		uint16 readSlot = 0;
		uint16 size = cursor.Available(BufferSize, readSlot);
		// Can't peek more than we have
		if (offset >= size) {
			LeaveSection();
			return 0;
		}

		uint16 available = size - offset;
		uint16 toPeek = (count < available) ? count : available;

		for (uint16 i = 0; i < toPeek; i++) {
			outBuffer[i] = storage.Data()[Cursor::Advance(readSlot, static_cast<uint32>(offset) + i, slotCount)];
		}

		LeaveSection();
		return toPeek;
	}


	// Mark elements as read but don't remove them yet
	ResultStatus MarkRead(uint16 count) {
		EnterSection();
		bool marked = cursor.MarkRead(BufferSize, count);
		LeaveSection();

		return marked ? ResultStatus::ok : ResultStatus::error;  // Can't mark more than available
	}


	// Commit all marked reads - actually remove them from buffer
	void CommitReads() {
		// One step, not one per element: a whole transfer can be committed at once, and the interrupts stay masked meanwhile.
		EnterSection();
		cursor.CommitReads(BufferSize);
		LeaveSection();
	}


	// Rollback marked reads - cancel uncommitted reads
	void RollbackReads() {
		EnterSection();
		cursor.RollbackReads();
		LeaveSection();
	}


	// Get number of uncommitted reads
	uint16 GetUncommittedCount() const {
		return cursor.Uncommitted();
	}
};
