#pragma once

#include <Utilities/Memory/Handle.h>
#include <System/System.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>

namespace Memory {

struct NoDefrag {
	template <typename Heap>
	static void Step(Heap&) {
	}
};

template <std::size_t N = 1>
struct IncrementalDefrag {
	template <typename Heap>
	static void Step(Heap& heap) {
		for (std::size_t i = 0; i < N; ++i) {
			if (!heap.DefragStep()) {
				break;
			}
		}
	}
};

template <std::size_t N = 1>
using BatchDefrag = IncrementalDefrag<N>;

template <std::size_t Percent>
struct ThresholdDefrag {
	template <typename Heap>
	static void Step(Heap& heap) {
		if (heap.FragmentationPercent() >= Percent) {
			heap.DefragStep();
		}
	}
};

struct NoLock {
	static void Lock() {
	}

	static void Unlock() {
	}
};

// Called when a request cannot be met. The default does nothing.
struct NoOom {
	static void Notify() {
	}
};

// Calls Handler when a request cannot be met.
template <void (*Handler)()>
struct OomHandler {
	static void Notify() {
		Handler();
	}
};

// The header of a block: 12 bytes on a 32-bit target. The data starts dataOffset bytes after the header. The offset
// is chosen when the block is placed and chosen again when the block changes position, so the data keeps its alignment. A
// free block has no entry, and its dataOffset is the size of the header.
struct BlockHeader {
	Entry*       entry;
	std::size_t  size;
	bool         free;
	bool         pinned;
	std::uint8_t alignLog2;
	std::uint8_t dataOffset;
};

// The heap manages a byte buffer. Blocks and entries live in it, so pointers returned by Handle::Data() are valid
// only until the next call that can move blocks: Defragment(), DefragStep(), Reallocate() or Deallocate().
// Only a Handle stays valid across Defragment(). A pinned block (AllocatePinned) is never moved, and it is freed by
// ReleasePinned(). Deallocate() and Reallocate() refuse a pinned block.
//
// The buffer constructor with a size allocates the buffer with the global operator new[] and frees it with
// operator delete[]. A port must provide both. The constructors that take a buffer need no global operator.
template <typename DefragPolicy = NoDefrag, typename LockPolicy = NoLock, typename OomPolicy = NoOom>
class Heap {
private:
	static constexpr std::size_t kBlockAlignment = 8;
	static constexpr std::uint8_t kBlockAlignmentLog2 = 3;
	static constexpr std::uint8_t kMaxAlignmentLog2 = 6;
	static constexpr std::size_t kHeaderSize = sizeof(BlockHeader);
	static constexpr std::size_t kEntryAlignment = alignof(Entry);
	static constexpr std::size_t NoBlock = static_cast<std::size_t>(-1);
#ifndef NDEBUG
	static constexpr std::uint32_t kMagic = 0x48454150u;
#endif

	std::uint8_t*    buffer = nullptr;
	std::size_t      capacity = 0;
	bool             ownsBuffer = false;
	std::size_t      dataTop = 0;
	std::size_t      entryCount = 0;
#ifndef NDEBUG
	std::uint32_t   magic = 0;
#endif

public:
	// External buffer, pointer + size. The heap starts at the first address that aligns entries, so the usable
	// capacity can be a few bytes less than size.
	Heap(std::uint8_t* buf, std::size_t size) {
		AdoptBuffer(buf, size);
		MarkConstructed();
	}

	// External buffer, array reference
	template <std::size_t N>
	explicit Heap(std::uint8_t (&buf)[N])
		: Heap(buf, N) {
	}

	// Internal buffer. It is made of 64-bit words, so its start is 8-byte aligned.
	explicit Heap(std::size_t size)
		: buffer(reinterpret_cast<std::uint8_t*>(new std::uint64_t[(size + sizeof(std::uint64_t) - 1) / sizeof(std::uint64_t)]))
		, capacity(AlignDown(size, kEntryAlignment))
		, ownsBuffer(true) {
		MarkConstructed();
	}

	~Heap() {
		if (ownsBuffer) {
			delete[] reinterpret_cast<std::uint64_t*>(buffer);
		}
#ifndef NDEBUG
		magic = 0;
#endif
	}

	// Non-copyable
	Heap(const Heap&) = delete;
	Heap& operator=(const Heap&) = delete;

	// Returns an invalid handle, and calls OomPolicy::Notify(), when no block of this size fits.
	Handle Allocate(std::size_t size) {
		AssertNotInterrupt();
		LockPolicy::Lock();
		Handle result = AllocateLocked(size, kBlockAlignmentLog2, false);
		LockPolicy::Unlock();

		if (!result.IsValid()) {
			OomPolicy::Notify();
		}
		return result;
	}

	// A freed block merges with the free blocks next to it, so two free neighbours never stand side by side.
	// A pinned block is not freed, and the handle keeps its block.
	void Deallocate(Handle& handle) {
		LockPolicy::Lock();
		DeallocateLocked(handle);
		LockPolicy::Unlock();
	}

	// Allocates the new block, copies the data and frees the old block under one lock, so Defragment() cannot
	// move the old block in between. Returns an invalid handle for a pinned block, which keeps its block.
	Handle Reallocate(Handle& handle, std::size_t newSize) {
		AssertNotInterrupt();
		LockPolicy::Lock();
		if (IsPinned(handle)) {
			LockPolicy::Unlock();
			return Handle();
		}

		Handle result = ReallocateLocked(handle, newSize);
		LockPolicy::Unlock();

		if (!result.IsValid()) {
			OomPolicy::Notify();
		}
		return result;
	}

	// Returns the data of a block that never moves, or nullptr when no block fits. The alignment is a power of two, up
	// to 64. The block stays until ReleasePinned() frees it.
	void* AllocatePinned(std::size_t size, std::size_t alignment) {
		return AllocatePinnedHandle(size, alignment).Data();
	}

	// The handle of a pinned block. Deallocate() and Reallocate() refuse it, so only ReleasePinned() frees the block.
	Handle AllocatePinnedHandle(std::size_t size, std::size_t alignment) {
		AssertNotInterrupt();
		std::uint8_t alignLog2 = 0;
		if (!AlignmentLog2(alignment, alignLog2)) {
			return Handle();
		}

		LockPolicy::Lock();
		Handle result = AllocateLocked(size, alignLog2, true);
		LockPolicy::Unlock();

		if (!result.IsValid()) {
			OomPolicy::Notify();
		}
		return result;
	}

	// Frees a block that AllocatePinned() returned. Other pointers are ignored.
	void ReleasePinned(void* data) {
		LockPolicy::Lock();
		std::size_t offset = FindBlock(data);
		if (offset != NoBlock && HeaderAt(offset)->pinned) {
			FreeBlock(offset);
		}
		LockPolicy::Unlock();
	}

	void Defragment() {
		LockPolicy::Lock();
		DefragPolicy::Step(*this);
		LockPolicy::Unlock();
	}

	// Moves the first movable block that sits behind a free block down over that free block. A pinned block never
	// changes position, and the free space in front of it stays where it is. Returns false when no block can move.
	bool DefragStep() {
		std::size_t offset = 0;
		while (offset + kHeaderSize <= dataTop) {
			BlockHeader* hdr = HeaderAt(offset);
			std::size_t next = offset + BlockTotal(hdr);

			if (hdr->free) {
				if (next >= dataTop) {
					dataTop = offset;
					return true;
				}

				// Adjacent free blocks never exist, so the block after this one is in use.
				if (!HeaderAt(next)->pinned && MoveBlockDown(offset, next)) {
					return true;
				}
			}

			offset = next;
		}
		return false;
	}

	// Free bytes between the used blocks, as a share of the used region, 0 to 100.
	std::size_t FragmentationPercent() const {
		std::size_t totalFree = 0;
		std::size_t offset = 0;

		while (offset + kHeaderSize <= dataTop) {
			const BlockHeader* hdr = HeaderAt(offset);
			if (hdr->free) {
				totalFree += hdr->size;
			}
			offset += BlockTotal(hdr);
		}

		if (totalFree == 0) return 0;
		if (dataTop == 0) return 0;
		return (totalFree * 100) / dataTop;
	}

	// Bytes of data in used blocks, pinned blocks included.
	std::size_t UsedBytes() const {
		std::size_t used = 0;
		std::size_t offset = 0;
		while (offset + kHeaderSize <= dataTop) {
			const BlockHeader* hdr = HeaderAt(offset);
			if (!hdr->free) {
				used += hdr->size;
			}
			offset += BlockTotal(hdr);
		}
		return used;
	}

	std::size_t TotalBytes() const {
		return capacity;
	}

	// The bytes a request can use: the unallocated tail, less the entry table, plus the free blocks.
	std::size_t FreeBytes() const {
		std::size_t tableSz = entryCount * sizeof(Entry);
		std::size_t unallocated = capacity - dataTop - tableSz;

		std::size_t freeInBlocks = 0;
		std::size_t offset = 0;
		while (offset + kHeaderSize <= dataTop) {
			const BlockHeader* hdr = HeaderAt(offset);
			if (hdr->free) {
				freeInBlocks += hdr->size;
			}
			offset += BlockTotal(hdr);
		}

		return unallocated + freeInBlocks;
	}

	// False when the heap object was not constructed, which a static object in another unit can see while its
	// constructor has not run. Always true when NDEBUG is defined.
	bool IsConstructed() const {
#ifndef NDEBUG
		return magic == kMagic;
#else
		return true;
#endif
	}

private:
	static constexpr std::size_t AlignUp(std::size_t value) {
		return (value + kBlockAlignment - 1) & ~(kBlockAlignment - 1);
	}

	static constexpr std::size_t AlignDown(std::size_t value, std::size_t alignment) {
		return value - value % alignment;
	}

	// Data offset from a header at headerAddress, for data aligned to 2 to the power of alignLog2.
	static std::size_t AlignedDataOffset(std::uintptr_t headerAddress, std::uint8_t alignLog2) {
		std::uintptr_t alignment = static_cast<std::uintptr_t>(1) << alignLog2;
		std::uintptr_t data = (headerAddress + kHeaderSize + alignment - 1) & ~(alignment - 1);
		return static_cast<std::size_t>(data - headerAddress);
	}

	static bool AlignmentLog2(std::size_t alignment, std::uint8_t& alignLog2) {
		for (std::uint8_t bits = 0; bits <= kMaxAlignmentLog2; ++bits) {
			if (alignment == (static_cast<std::size_t>(1) << bits)) {
				alignLog2 = bits;
				return true;
			}
		}
		return false;
	}

	static std::size_t BlockTotal(const BlockHeader* hdr) {
		return hdr->dataOffset + hdr->size;
	}

	// Starts the heap at the first address that aligns entries. Bytes at lower addresses than that are not used.
	void AdoptBuffer(std::uint8_t* buf, std::size_t size) {
		std::size_t misalignment = reinterpret_cast<std::uintptr_t>(buf) % kEntryAlignment;
		std::size_t skip = misalignment == 0 ? 0 : kEntryAlignment - misalignment;
		if (skip > size) {
			skip = size;
		}

		buffer = buf + skip;
		capacity = AlignDown(size - skip, kEntryAlignment);
	}

	void MarkConstructed() {
#ifndef NDEBUG
		magic = kMagic;
#endif
	}

	static void AssertNotInterrupt() {
#ifndef NDEBUG
		SystemAssert(!System::IsInterrupt(), "a heap block is allocated in an interrupt");
#endif
	}

	BlockHeader* HeaderAt(std::size_t offset) const {
		return reinterpret_cast<BlockHeader*>(buffer + offset);
	}

	// Offset of the header of the used block whose data starts at data, or NoBlock when there is none. Walks the
	// blocks, so it costs one step per block.
	std::size_t FindBlock(const void* data) const {
		const std::uint8_t* target = static_cast<const std::uint8_t*>(data);
		std::size_t offset = 0;
		while (offset + kHeaderSize <= dataTop) {
			const BlockHeader* hdr = HeaderAt(offset);
			if (!hdr->free && buffer + offset + hdr->dataOffset == target) {
				return offset;
			}
			offset += BlockTotal(hdr);
		}
		return NoBlock;
	}

	bool IsPinned(const Handle& handle) const {
		Entry* entry = handle.GetEntry();
		if (entry == nullptr) {
			return false;
		}
		std::size_t offset = FindBlock(entry->data);
		return offset != NoBlock && HeaderAt(offset)->pinned;
	}

	// Offset of the block that ends where the block at target starts, or NoBlock for the first block. Walks the
	// blocks from the start, so it costs one step per block up to target.
	std::size_t PreviousBlockOffset(std::size_t target) const {
		std::size_t previous = NoBlock;
		std::size_t offset = 0;
		while (offset < target) {
			previous = offset;
			offset += BlockTotal(HeaderAt(offset));
		}
		return previous;
	}

	// The caller holds the lock. Frees the block at offset: its entry is deactivated and its header becomes a free
	// header that keeps the total size of the block.
	void FreeBlock(std::size_t offset) {
		BlockHeader* hdr = HeaderAt(offset);
		std::size_t total = BlockTotal(hdr);
		hdr->entry->Deactivate();
		hdr->entry = nullptr;
		hdr->free = true;
		hdr->pinned = false;
		hdr->size = total - kHeaderSize;
		hdr->dataOffset = static_cast<std::uint8_t>(kHeaderSize);
		hdr->alignLog2 = 0;
		MergeFreeBlock(offset);
	}

	// The caller holds the lock. Frees the block of handle and clears handle. Other copies of the handle become
	// invalid. A pinned block is left as it is.
	void DeallocateLocked(Handle& handle) {
		Entry* entry = handle.GetEntry();
		if (entry == nullptr) {
			return;
		}

		std::size_t offset = FindBlock(entry->data);
		if (offset == NoBlock || HeaderAt(offset)->pinned) {
			return;
		}

		FreeBlock(offset);
		handle = Handle();
	}

	// The caller holds the lock.
	Handle ReallocateLocked(Handle& handle, std::size_t newSize) {
		if (!handle.IsValid()) {
			return AllocateLocked(newSize, kBlockAlignmentLog2, false);
		}

		std::size_t oldSize = handle.Size();
		if (newSize <= oldSize) {
			return handle;
		}

		std::uint8_t alignLog2 = HeaderAt(FindBlock(handle.Data()))->alignLog2;
		Handle newHandle = AllocateLocked(newSize, alignLog2, false);
		if (!newHandle.IsValid()) {
			return Handle();
		}

		memcpy(newHandle.Data(), handle.Data(), oldSize);
		DeallocateLocked(handle);

		return newHandle;
	}

	// The caller holds the lock. Returns an invalid handle when no block of this size fits. The first free block that
	// fits is used, and the space after the used data is the fallback.
	Handle AllocateLocked(std::size_t size, std::uint8_t alignLog2, bool pinned) {
		if (size > capacity) {
			return Handle();
		}

		std::size_t rounded = AlignUp(size);
		std::size_t offset = 0;
		while (offset + kHeaderSize <= dataTop) {
			BlockHeader* hdr = HeaderAt(offset);
			std::size_t total = BlockTotal(hdr);

			if (hdr->free) {
				std::size_t dataOffset = AlignedDataOffset(reinterpret_cast<std::uintptr_t>(buffer + offset), alignLog2);
				if (dataOffset + rounded <= total) {
					Handle result = PlaceInFreeBlock(offset, dataOffset, rounded, alignLog2, pinned);
					if (result.IsValid()) {
						return result;
					}
				}
			}

			offset += total;
		}

		return AllocateAtTail(rounded, alignLog2, pinned);
	}

	// The caller holds the lock. The block takes the start of the free block at offset, and the rest of the free
	// block stays free when it is large enough for a header. Returns an invalid handle when no entry is left.
	Handle PlaceInFreeBlock(std::size_t offset, std::size_t dataOffset, std::size_t rounded, std::uint8_t alignLog2,
		bool pinned) {
		BlockHeader* hdr = HeaderAt(offset);
		std::size_t total = BlockTotal(hdr);
		std::size_t leftover = total - dataOffset - rounded;
		if (leftover <= kHeaderSize) {
			leftover = 0;
		}

		Entry* entry = TakeEntry(dataTop);
		if (entry == nullptr) {
			return Handle();
		}

		std::size_t usedTotal = total - leftover;
		std::size_t usedSize = usedTotal - dataOffset;
		hdr->entry = entry;
		hdr->size = usedSize;
		hdr->free = false;
		hdr->pinned = pinned;
		hdr->alignLog2 = alignLog2;
		hdr->dataOffset = static_cast<std::uint8_t>(dataOffset);

		if (leftover != 0) {
			BlockHeader* rest = HeaderAt(offset + usedTotal);
			rest->entry = nullptr;
			rest->size = leftover - kHeaderSize;
			rest->free = true;
			rest->pinned = false;
			rest->alignLog2 = 0;
			rest->dataOffset = static_cast<std::uint8_t>(kHeaderSize);
		}

		entry->Activate(buffer + offset + dataOffset, usedSize);
		return Handle(entry);
	}

	// The caller holds the lock. The block goes after the used data. A freed entry slot is reused, so the entry table
	// does not grow with each allocation and free.
	Handle AllocateAtTail(std::size_t rounded, std::uint8_t alignLog2, bool pinned) {
		std::size_t dataOffset = AlignedDataOffset(reinterpret_cast<std::uintptr_t>(buffer + dataTop), alignLog2);
		std::size_t total = dataOffset + rounded;

		Entry* entry = FindDeadEntry();
		std::size_t newEntryCount = entry != nullptr ? entryCount : entryCount + 1;
		if (dataTop + total + newEntryCount * sizeof(Entry) > capacity) {
			return Handle();
		}

		if (entry == nullptr) {
			entry = EntrySlot(entryCount);
			new (entry) Entry();
			entryCount = newEntryCount;
		}

		BlockHeader* hdr = HeaderAt(dataTop);
		hdr->entry = entry;
		hdr->size = rounded;
		hdr->free = false;
		hdr->pinned = pinned;
		hdr->alignLog2 = alignLog2;
		hdr->dataOffset = static_cast<std::uint8_t>(dataOffset);

		entry->Activate(buffer + dataTop + dataOffset, rounded);
		dataTop += total;
		return Handle(entry);
	}

	// Takes an entry slot for a block that is placed at dataEnd, the end of the used data. Returns nullptr when the
	// table cannot grow without meeting the data.
	Entry* TakeEntry(std::size_t dataEnd) {
		Entry* entry = FindDeadEntry();
		if (entry != nullptr) {
			return entry;
		}

		if (dataEnd + (entryCount + 1) * sizeof(Entry) > capacity) {
			return nullptr;
		}

		entry = EntrySlot(entryCount);
		new (entry) Entry();
		entryCount++;
		return entry;
	}

	// The caller holds the lock. Called for a block that has just become free. Merges it with a free block that precedes it
	// and with the free blocks after it. A free block that ends the used region returns its space to the tail.
	void MergeFreeBlock(std::size_t offset) {
		std::size_t previous = PreviousBlockOffset(offset);
		if (previous != NoBlock && HeaderAt(previous)->free) {
			HeaderAt(previous)->size += BlockTotal(HeaderAt(offset));
			offset = previous;
		}

		BlockHeader* hdr = HeaderAt(offset);
		std::size_t next = offset + BlockTotal(hdr);
		while (next < dataTop && HeaderAt(next)->free) {
			hdr->size += BlockTotal(HeaderAt(next));
			next = offset + BlockTotal(hdr);
		}

		if (next >= dataTop) {
			dataTop = offset;
		}
	}

	// Moves the used block at blockOffset down to freeOffset, the free block in front of it. The block keeps its
	// alignment, so its new data offset may differ. Returns false when the block does not fit in the two blocks.
	bool MoveBlockDown(std::size_t freeOffset, std::size_t blockOffset) {
		BlockHeader* blockHdr = HeaderAt(blockOffset);
		std::size_t regionLen = blockOffset - freeOffset + BlockTotal(blockHdr);
		std::size_t newDataOffset = AlignedDataOffset(reinterpret_cast<std::uintptr_t>(buffer + freeOffset),
			blockHdr->alignLog2);
		std::size_t newTotal = newDataOffset + blockHdr->size;
		if (newTotal > regionLen) {
			return false;
		}

		// A remainder too small for a header joins the block.
		std::size_t leftover = regionLen - newTotal;
		std::size_t usedSize = blockHdr->size;
		if (leftover <= kHeaderSize) {
			usedSize += leftover;
			newTotal += leftover;
			leftover = 0;
		}

		Entry* entry = blockHdr->entry;
		std::uint8_t alignLog2 = blockHdr->alignLog2;
		bool pinned = blockHdr->pinned;
		std::uint8_t* source = buffer + blockOffset + blockHdr->dataOffset;
		std::uint8_t* destination = buffer + freeOffset + newDataOffset;
		std::memmove(destination, source, blockHdr->size);

		BlockHeader* hdr = HeaderAt(freeOffset);
		hdr->entry = entry;
		hdr->size = usedSize;
		hdr->free = false;
		hdr->pinned = pinned;
		hdr->alignLog2 = alignLog2;
		hdr->dataOffset = static_cast<std::uint8_t>(newDataOffset);
		entry->data = destination;
		entry->size = usedSize;

		if (leftover != 0) {
			BlockHeader* rest = HeaderAt(freeOffset + newTotal);
			rest->entry = nullptr;
			rest->size = leftover - kHeaderSize;
			rest->free = true;
			rest->pinned = false;
			rest->alignLog2 = 0;
			rest->dataOffset = static_cast<std::uint8_t>(kHeaderSize);
			MergeFreeBlock(freeOffset + newTotal);
		}
		return true;
	}

	Entry* EntrySlot(std::size_t index) {
		return reinterpret_cast<Entry*>(buffer + capacity - (index + 1) * sizeof(Entry));
	}

	// A slot whose block was freed, or nullptr when every slot is in use. Reusing it keeps the table from growing.
	Entry* FindDeadEntry() {
		for (std::size_t i = 0; i < entryCount; ++i) {
			Entry* e = EntrySlot(i);
			if (!e->alive) {
				return e;
			}
		}
		return nullptr;
	}
};

}
