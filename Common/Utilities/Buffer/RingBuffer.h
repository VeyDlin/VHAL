#pragma once
#include <VHAL.h>
#include <Utilities/Memory/Storage.h>
#include <algorithm>
#include <concepts>
#include <limits>
#include <span>
#include <utility>


// A ring of BufferSize elements. Policy says where the elements are kept (Memory::Inside, Memory::FromHeap or Memory::External).
template <typename ElementType, uint16 BufferSize, typename Policy = Memory::Inside>
class RingBuffer {
	static_assert(BufferSize > 0, "RingBuffer with size 0 are forbidden");


private:
	typename Policy::template Storage<ElementType, BufferSize> storage;
	uint16 readIndex;
	uint16 size;
	uint16 uncommittedReadCount = 0;  // How many reads are uncommitted


private:
	uint16 WriteIndex() {
		uint32 writeIndex = readIndex + size;
		if (writeIndex >= BufferSize) {
			writeIndex -= BufferSize;
		}
		if (size < BufferSize) {
			size++;
		}
		return static_cast<uint16>(writeIndex);
	}


	uint16 ReadIndex() {
		uint16 out = readIndex;
		readIndex = (readIndex + 1) % BufferSize;
		if (size > 0) {
			size--;
		}
		return out;
	}


public:
	RingBuffer() : readIndex(0), size(0) {}


	RingBuffer(const RingBuffer&) = default;
	RingBuffer& operator=(const RingBuffer&) = default;


	// A move takes the elements with the storage. The source is left empty, so its indexes and marks are reset with them.
	RingBuffer(RingBuffer&& other) noexcept
		: storage(std::move(other.storage))
		, readIndex(other.readIndex)
		, size(other.size)
		, uncommittedReadCount(other.uncommittedReadCount) {
		other.readIndex = 0;
		other.size = 0;
		other.uncommittedReadCount = 0;
	}


	RingBuffer& operator=(RingBuffer&& other) noexcept {
		if (this != &other) {
			storage = std::move(other.storage);
			readIndex = other.readIndex;
			size = other.size;
			uncommittedReadCount = other.uncommittedReadCount;
			other.readIndex = 0;
			other.size = 0;
			other.uncommittedReadCount = 0;
		}

		return *this;
	}


	// The arguments go to the policy's storage, for example the memory of Memory::External.
	template <typename... Arguments>
		requires(sizeof...(Arguments) > 0 && std::constructible_from<typename Policy::template Storage<ElementType, BufferSize>, Arguments...>)
	explicit RingBuffer(Arguments&&... arguments)
		: storage(std::forward<Arguments>(arguments)...)
		, readIndex(0)
		, size(0) {
	}


	ResultStatus Push(const ElementType inElement) {
		if (!storage.Ready()) {
			return ResultStatus::notReady;
		}

		System::CriticalSection(true);
		if (IsFull()) {
			System::CriticalSection(false);
			return ResultStatus::filled;
		}

		storage.Data()[WriteIndex()] = inElement;
		System::CriticalSection(false);

		return ResultStatus::ok;
	}


	ResultStatus Push(const ElementType* const inElement) {
		if (!inElement) {
			return ResultStatus::error;
		}

		if (!storage.Ready()) {
			return ResultStatus::notReady;
		}

		System::CriticalSection(true);
		if (IsFull()) {
			System::CriticalSection(false);
			return ResultStatus::filled;
		}

		storage.Data()[WriteIndex()] = *inElement;
		System::CriticalSection(false);

		return ResultStatus::ok;
	}


	// Pushes as many of elements as fit and returns how many were pushed. The whole copy runs in one critical section, where a
	// Push() per element would take one each. The copy is at most two segments: up to the end of the buffer and from its start.
	uint16 Push(std::span<const ElementType> elements) {
		if (!storage.Ready()) {
			return 0;
		}

		System::CriticalSection(true);

		uint32 count = static_cast<uint32>(elements.size());
		uint32 freeSpace = BufferSize - size;
		if (count > freeSpace) {
			count = freeSpace;
		}

		uint32 writeIndex = static_cast<uint32>(readIndex) + size;
		if (writeIndex >= BufferSize) {
			writeIndex -= BufferSize;
		}

		uint32 firstSegment = BufferSize - writeIndex;
		if (firstSegment > count) {
			firstSegment = count;
		}

		ElementType* items = storage.Data();
		std::copy_n(elements.data(), firstSegment, &items[writeIndex]);
		if (count > firstSegment) {
			std::copy_n(elements.data() + firstSegment, count - firstSegment, items);
		}
		size = static_cast<uint16>(size + count);
		System::CriticalSection(false);

		return static_cast<uint16>(count);
	}


	Result<ElementType> Pop() {
		System::CriticalSection(true);
		// Marked elements must stay in place until CommitReads or RollbackReads
		if (uncommittedReadCount > 0) {
			System::CriticalSection(false);
			return ResultStatus::busy;
		}
		if (IsEmpty()) {
			System::CriticalSection(false);
			return ResultStatus::empty;
		}

		ElementType data = storage.Data()[ReadIndex()];
		System::CriticalSection(false);

		return data;
	}


	// Pops up to out.size() elements into out and returns how many were popped. Like Pop(), it refuses with 0 while marked reads
	// are uncommitted. The copy runs in one critical section, in at most two segments, so keep out to what the caller can afford
	// to copy with interrupts masked.
	uint16 Pop(std::span<ElementType> out) {
		System::CriticalSection(true);
		// Marked elements must stay in place until CommitReads or RollbackReads
		if (uncommittedReadCount > 0) {
			System::CriticalSection(false);
			return 0;
		}

		uint32 count = static_cast<uint32>(out.size());
		if (count > size) {
			count = size;
		}

		uint32 firstSegment = BufferSize - readIndex;
		if (firstSegment > count) {
			firstSegment = count;
		}

		ElementType* items = storage.Data();
		std::copy_n(&items[readIndex], firstSegment, out.data());
		std::copy_n(items, count - firstSegment, out.data() + firstSegment);
		readIndex = static_cast<uint16>((readIndex + count) % BufferSize);
		size = static_cast<uint16>(size - count);
		System::CriticalSection(false);

		return static_cast<uint16>(count);
	}


	bool IsFull() const {
		return size == BufferSize;
	}


	bool IsEmpty() const {
		return size == 0;
	}


	// Empties the ring and drops its marks too. It is the reset path: a reset must not keep the bytes of the old session.
	void Clear() {
		System::CriticalSection(true);
		readIndex = 0;
		size = 0;
		uncommittedReadCount = 0;
		System::CriticalSection(false);
	}


	uint16 Size() const {
		return size;
	}


	constexpr uint16 MaxSize() const {
		return BufferSize;
	}


	uint16 GetFreeSpace() const {
		return BufferSize - size;
	}


	Result<ElementType> Peek() const {
		if (IsEmpty()) {
			return ResultStatus::empty;
		}
		return storage.Data()[readIndex];
	}


	ElementType operator[](uint16 inIndex) const {
		SystemAssert(inIndex < size);

		System::CriticalSection(true);
		uint32 index = static_cast<uint32>(readIndex) + inIndex;
		if (index >= BufferSize) {
			index -= BufferSize;
		}
		ElementType data = storage.Data()[static_cast<uint16>(index)];
		System::CriticalSection(false);

		return data;
	}


	// Peek at multiple elements without removing them
	// Returns actual number of elements peeked (may be less than requested)
	uint16 PeekMultiple(ElementType* outBuffer, uint16 count, uint16 offset = 0) const {
		System::CriticalSection(true);

		// Can't peek more than we have
		if (offset >= size) {
			System::CriticalSection(false);
			return 0;
		}

		uint16 available = size - offset;
		uint16 toPeek = (count < available) ? count : available;

		for (uint16 i = 0; i < toPeek; i++) {
			uint32 index = static_cast<uint32>(readIndex) + offset + i;
			if (index >= BufferSize) {
				index -= BufferSize;
			}
			outBuffer[i] = storage.Data()[static_cast<uint16>(index)];
		}

		System::CriticalSection(false);
		return toPeek;
	}


	// Mark elements as read but don't remove them yet
	ResultStatus MarkRead(uint16 count) {
		System::CriticalSection(true);

		if (count > size - uncommittedReadCount) {
			System::CriticalSection(false);
			return ResultStatus::error;  // Can't mark more than available
		}

		uncommittedReadCount += count;

		System::CriticalSection(false);
		return ResultStatus::ok;
	}


	// Commit all marked reads - actually remove them from buffer
	void CommitReads() {
		// One step, not one per element: a whole transfer can be committed at once, and the interrupts stay masked meanwhile.
		System::CriticalSection(true);
		uint32 nextIndex = static_cast<uint32>(readIndex) + uncommittedReadCount;
		if (nextIndex >= BufferSize) {
			nextIndex -= BufferSize;
		}

		readIndex = static_cast<uint16>(nextIndex);
		size = static_cast<uint16>(size - uncommittedReadCount);
		uncommittedReadCount = 0;
		System::CriticalSection(false);
	}


	// Rollback marked reads - cancel uncommitted reads
	void RollbackReads() {
		System::CriticalSection(true);
		uncommittedReadCount = 0;
		System::CriticalSection(false);
	}


	// Get number of uncommitted reads
	uint16 GetUncommittedCount() const {
		return uncommittedReadCount;
	}
};
