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
// The locked mode takes a critical section for each access and keeps a read index and a count, so its storage holds BufferSize
// elements. The singleWriter and callerLocked modes keep head and tail indexes (RingIndexes), which leave one slot empty: their storage
// holds BufferSize + 1 elements, and Memory::External needs that many. The singleWriter mode is for one producer context, which
// calls Push, and one consumer context, which calls every other member; it takes no critical section. The callerLocked mode takes no
// critical section either, and the caller serializes every access.
//
// The pointers from Front() and Peek(index) stay valid until the element is popped or dropped. In locked mode the caller makes sure no
// other context pops meanwhile.
template <typename ElementType, uint16 BufferSize, typename Policy = Memory::Inside, RingAccess Access = RingAccess::locked>
class RingBuffer {
	static_assert(BufferSize > 0, "RingBuffer with size 0 are forbidden");
	static_assert(
		Access == RingAccess::locked || BufferSize < std::numeric_limits<uint16>::max(),
		"A singleWriter or callerLocked RingBuffer keeps N + 1 indexes: N must be below 65535");


private:
	static constexpr bool isLocked = Access == RingAccess::locked;

	// The modulus of the index arithmetic: the elements of a locked ring, one more in the other modes.
	static constexpr uint16 slotCount = isLocked ? BufferSize : static_cast<uint16>(BufferSize + 1);
	static constexpr std::size_t storageCount = static_cast<std::size_t>(slotCount);

	// A singleWriter ring publishes an index with release and reads the other context's index with acquire, so the data of an
	// element is visible before its index. The other modes need no barrier.
	static constexpr std::memory_order publishOrder =
		Access == RingAccess::singleWriter ? std::memory_order_release : std::memory_order_relaxed;
	static constexpr std::memory_order observeOrder =
		Access == RingAccess::singleWriter ? std::memory_order_acquire : std::memory_order_relaxed;

	typename Policy::template Storage<ElementType, storageCount> storage;
	std::conditional_t<isLocked, LockedCursor, RingIndexes> indexes;
	uint16 uncommittedReadCount = 0;  // How many reads are uncommitted


private:
	// The count of elements. A locked ring keeps it; the other modes derive it from the two indexes.
	uint16 Count() const {
		if constexpr (isLocked) {
			return indexes.size;
		} else {
			return RingIndexes::Held(indexes.head.load(observeOrder), indexes.tail.load(observeOrder), slotCount);
		}
	}


	// The slot of the oldest element.
	uint16 ReadSlot() const {
		if constexpr (isLocked) {
			return indexes.readIndex;
		} else {
			return indexes.tail.load(std::memory_order_relaxed);
		}
	}


	// The slot where the next element goes.
	uint16 WriteSlot() const {
		if constexpr (isLocked) {
			return RingIndexes::Advance(indexes.readIndex, indexes.size, slotCount);
		} else {
			return indexes.head.load(std::memory_order_relaxed);
		}
	}


	// Publishes count elements written at the back.
	void Produce(uint16 count) {
		if constexpr (isLocked) {
			indexes.size = static_cast<uint16>(indexes.size + count);
		} else {
			indexes.head.store(RingIndexes::Advance(WriteSlot(), count, slotCount), publishOrder);
		}
	}


	// Releases count elements from the front. The data of them must be read before this call.
	void Consume(uint16 count) {
		if constexpr (isLocked) {
			indexes.readIndex = RingIndexes::Advance(indexes.readIndex, count, slotCount);
			indexes.size = static_cast<uint16>(indexes.size - count);
		} else {
			indexes.tail.store(RingIndexes::Advance(ReadSlot(), count, slotCount), publishOrder);
		}
	}


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
	RingBuffer() {}


	RingBuffer(const RingBuffer&) = default;
	RingBuffer& operator=(const RingBuffer&) = default;


	// A move takes the elements with the storage. The source is left empty, so its indexes and marks are reset with them.
	RingBuffer(RingBuffer&& other) noexcept
		: storage(std::move(other.storage))
		, indexes(other.indexes)
		, uncommittedReadCount(other.uncommittedReadCount) {
		other.indexes.Reset();
		other.uncommittedReadCount = 0;
	}


	RingBuffer& operator=(RingBuffer&& other) noexcept {
		if (this != &other) {
			storage = std::move(other.storage);
			indexes = other.indexes;
			uncommittedReadCount = other.uncommittedReadCount;
			other.indexes.Reset();
			other.uncommittedReadCount = 0;
		}

		return *this;
	}


	// The arguments go to the policy's storage, for example the memory of Memory::External.
	template <typename... Arguments>
		requires(sizeof...(Arguments) > 0 && std::constructible_from<typename Policy::template Storage<ElementType, storageCount>, Arguments...>)
	explicit RingBuffer(Arguments&&... arguments)
		: storage(std::forward<Arguments>(arguments)...) {
	}


	// Producer: pushes one element at the back.
	ResultStatus Push(const ElementType inElement) {
		if (!storage.Ready()) {
			return ResultStatus::notReady;
		}

		EnterSection();
		if (IsFull()) {
			LeaveSection();
			return ResultStatus::filled;
		}

		storage.Data()[WriteSlot()] = inElement;
		Produce(1);
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

		uint32 count = static_cast<uint32>(elements.size());
		uint32 freeSpace = BufferSize - Count();
		if (count > freeSpace) {
			count = freeSpace;
		}

		uint32 writeIndex = WriteSlot();
		uint32 firstSegment = slotCount - writeIndex;
		if (firstSegment > count) {
			firstSegment = count;
		}

		ElementType* items = storage.Data();
		std::copy_n(elements.data(), firstSegment, &items[writeIndex]);
		if (count > firstSegment) {
			std::copy_n(elements.data() + firstSegment, count - firstSegment, items);
		}
		Produce(static_cast<uint16>(count));
		LeaveSection();

		return static_cast<uint16>(count);
	}


	// Consumer: pops the oldest element.
	Result<ElementType> Pop() {
		EnterSection();
		// Marked elements must stay in place until CommitReads or RollbackReads
		if (uncommittedReadCount > 0) {
			LeaveSection();
			return ResultStatus::busy;
		}
		if (IsEmpty()) {
			LeaveSection();
			return ResultStatus::empty;
		}

		ElementType data = storage.Data()[ReadSlot()];
		Consume(1);
		LeaveSection();

		return data;
	}


	// Consumer: pops up to out.size() elements into out and returns how many were popped. Like Pop(), it refuses with 0 while marked reads
	// are uncommitted. The copy runs in one critical section, in at most two segments, so keep out to what the caller can afford
	// to copy with interrupts masked.
	uint16 Pop(std::span<ElementType> out) {
		EnterSection();
		// Marked elements must stay in place until CommitReads or RollbackReads
		if (uncommittedReadCount > 0) {
			LeaveSection();
			return 0;
		}

		uint32 count = static_cast<uint32>(out.size());
		if (count > Count()) {
			count = Count();
		}

		uint32 readIndex = ReadSlot();
		uint32 firstSegment = slotCount - readIndex;
		if (firstSegment > count) {
			firstSegment = count;
		}

		ElementType* items = storage.Data();
		std::copy_n(&items[readIndex], firstSegment, out.data());
		std::copy_n(items, count - firstSegment, out.data() + firstSegment);
		Consume(static_cast<uint16>(count));
		LeaveSection();

		return static_cast<uint16>(count);
	}


	// Consumer: removes up to count of the oldest elements without copying them, and returns how many were removed. Like Pop(), it
	// removes nothing while marked reads are uncommitted.
	uint16 Drop(uint16 count) {
		EnterSection();
		if (uncommittedReadCount > 0) {
			LeaveSection();
			return 0;
		}

		uint16 available = Count();
		uint16 dropped = count < available ? count : available;
		Consume(dropped);
		LeaveSection();

		return dropped;
	}


	// Consumer: the oldest element, in place, or nullptr when the ring is empty. Marked elements are included.
	const ElementType* Front() const {
		EnterSection();
		if (Count() == 0) {
			LeaveSection();
			return nullptr;
		}

		const ElementType* item = &storage.Data()[ReadSlot()];
		LeaveSection();

		return item;
	}


	// Consumer: the element at index (0 is the oldest), in place, or nullptr when index is not held.
	const ElementType* Peek(uint16 index) const {
		EnterSection();
		if (index >= Count()) {
			LeaveSection();
			return nullptr;
		}

		const ElementType* item = &storage.Data()[RingIndexes::Advance(ReadSlot(), index, slotCount)];
		LeaveSection();

		return item;
	}


	bool IsFull() const {
		return Count() == BufferSize;
	}


	bool IsEmpty() const {
		return Count() == 0;
	}


	// Empties the ring and drops its marks too. It is the reset path: a reset must not keep the bytes of the old session.
	void Clear() {
		static_assert(
			Access != RingAccess::singleWriter,
			"RingBuffer::Clear is not offered in singleWriter mode: the consumer empties the ring with Drop(Size())");

		EnterSection();
		indexes.Reset();
		uncommittedReadCount = 0;
		LeaveSection();
	}


	uint16 Size() const {
		return Count();
	}


	constexpr uint16 MaxSize() const {
		return BufferSize;
	}


	uint16 GetFreeSpace() const {
		return static_cast<uint16>(BufferSize - Count());
	}


	Result<ElementType> Peek() const {
		if (IsEmpty()) {
			return ResultStatus::empty;
		}
		return storage.Data()[ReadSlot()];
	}


	ElementType operator[](uint16 inIndex) const {
		SystemAssert(inIndex < Count());

		EnterSection();
		ElementType data = storage.Data()[RingIndexes::Advance(ReadSlot(), inIndex, slotCount)];
		LeaveSection();

		return data;
	}


	// Peek at multiple elements without removing them
	// Returns actual number of elements peeked (may be less than requested)
	uint16 PeekMultiple(ElementType* outBuffer, uint16 count, uint16 offset = 0) const {
		EnterSection();

		uint16 size = Count();
		// Can't peek more than we have
		if (offset >= size) {
			LeaveSection();
			return 0;
		}

		uint16 available = size - offset;
		uint16 toPeek = (count < available) ? count : available;
		uint16 readSlot = ReadSlot();

		for (uint16 i = 0; i < toPeek; i++) {
			outBuffer[i] = storage.Data()[RingIndexes::Advance(readSlot, static_cast<uint32>(offset) + i, slotCount)];
		}

		LeaveSection();
		return toPeek;
	}


	// Mark elements as read but don't remove them yet
	ResultStatus MarkRead(uint16 count) {
		EnterSection();

		if (count > Count() - uncommittedReadCount) {
			LeaveSection();
			return ResultStatus::error;  // Can't mark more than available
		}

		uncommittedReadCount += count;

		LeaveSection();
		return ResultStatus::ok;
	}


	// Commit all marked reads - actually remove them from buffer
	void CommitReads() {
		// One step, not one per element: a whole transfer can be committed at once, and the interrupts stay masked meanwhile.
		EnterSection();
		Consume(uncommittedReadCount);
		uncommittedReadCount = 0;
		LeaveSection();
	}


	// Rollback marked reads - cancel uncommitted reads
	void RollbackReads() {
		EnterSection();
		uncommittedReadCount = 0;
		LeaveSection();
	}


	// Get number of uncommitted reads
	uint16 GetUncommittedCount() const {
		return uncommittedReadCount;
	}
};
