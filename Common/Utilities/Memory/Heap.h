#pragma once

#include <cstdint>
#include <cstddef>
#include <string.h>
#include <new>
#include <Utilities/Memory/IAllocator.h>

namespace Memory {

struct NoDefrag {
    template <typename Heap>
    static void Step(Heap&) {}
};

template <std::size_t N = 1>
struct IncrementalDefrag {
    template <typename Heap>
    static void Step(Heap& heap) {
        for (std::size_t i = 0; i < N; ++i) {
            if (!heap.DefragStep()) break;
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
    static void Lock() {}
    static void Unlock() {}
};

struct BlockHeader {
    Entry*      handleEntry = nullptr;
    std::size_t size = 0;
    bool        free = false;
};

class OomCallback {
private:
    static constexpr std::size_t StorageSize = 32;
    alignas(void*) unsigned char storage_[StorageSize] = {};
    void (*invoke_)(void*) = nullptr;
    void (*destroy_)(void*) = nullptr;

public:
    OomCallback() : invoke_(nullptr), destroy_(nullptr) {}

    template <typename F>
    OomCallback& operator=(F&& f) {
        Destroy();
        static_assert(sizeof(F) <= StorageSize, "OOM callback too large");
        new (&storage_) F(static_cast<F&&>(f));
        invoke_ = [](void* s) { (*static_cast<F*>(s))(); };
        destroy_ = [](void* s) { static_cast<F*>(s)->~F(); };
        return *this;
    }

    void operator()() const {
        if (invoke_) invoke_(const_cast<void*>(static_cast<const void*>(&storage_)));
    }

    explicit operator bool() const { return invoke_ != nullptr; }

    ~OomCallback() { Destroy(); }

    OomCallback(const OomCallback&) = delete;
    OomCallback& operator=(const OomCallback&) = delete;

private:
    void Destroy() {
        if (destroy_) {
            destroy_(&storage_);
            invoke_ = nullptr;
            destroy_ = nullptr;
        }
    }
};

// The heap manages a byte buffer. Blocks and entries live in it, so pointers returned by Handle::Data() are
// valid only until the next call that can move blocks: Defragment(), DefragStep(), Reallocate() or Deallocate().
// Only a Handle stays valid across Defragment().
//
// The buffer constructor with a size allocates the buffer with the global operator new[] and frees it with
// operator delete[]. A port must provide both. The constructors that take a buffer need no global operator.
template <typename DefragPolicy = NoDefrag, typename LockPolicy = NoLock>
class Heap : public IAllocator {
private:
    uint8_t*    buffer_;
    std::size_t capacity_;
    bool        ownsBuffer_;
    std::size_t dataTop_;
    std::size_t entryCount_;
    OomCallback oomCallback_;

public:
    // External buffer, pointer + size. The heap starts at the first address that aligns entries, so the usable
    // capacity can be a few bytes less than size.
    Heap(uint8_t* buf, std::size_t size)
        : buffer_(nullptr), capacity_(0), ownsBuffer_(false),
          dataTop_(0), entryCount_(0) {
        AdoptBuffer(buf, size);
    }

    // External buffer, array reference
    template <std::size_t N>
    explicit Heap(uint8_t (&buf)[N])
        : Heap(buf, N) {}

    // Internal buffer
    explicit Heap(std::size_t size)
        : buffer_(new uint8_t[size]), capacity_(AlignDown(size, kEntryAlignment)), ownsBuffer_(true),
          dataTop_(0), entryCount_(0) {}

    ~Heap() {
        if (ownsBuffer_) delete[] buffer_;
    }

    // Non-copyable
    Heap(const Heap&) = delete;
    Heap& operator=(const Heap&) = delete;

    Handle Allocate(std::size_t size) override {
        LockPolicy::Lock();
        Handle result = AllocateLocked(size);
        LockPolicy::Unlock();

        if (!result.IsValid() && oomCallback_) {
            oomCallback_();
        }
        return result;
    }

    // A freed block merges with the free blocks next to it, so two free neighbours never stand side by side.
    void Deallocate(Handle& handle) override {
        LockPolicy::Lock();
        DeallocateLocked(handle);
        LockPolicy::Unlock();
    }

    // Allocates the new block, copies the data and frees the old block under one lock, so Defragment() cannot
    // move the old block in between.
    Handle Reallocate(Handle& handle, std::size_t newSize) override {
        LockPolicy::Lock();
        Handle result = ReallocateLocked(handle, newSize);
        LockPolicy::Unlock();

        if (!result.IsValid() && oomCallback_) {
            oomCallback_();
        }
        return result;
    }

    template <typename F>
    void OnOutOfMemory(F&& callback) {
        oomCallback_ = static_cast<F&&>(callback);
    }

    void Defragment() {
        LockPolicy::Lock();
        DefragPolicy::Step(*this);
        LockPolicy::Unlock();
    }

    // Moves the first used block that sits behind a free block down over that free block. Returns false when
    // no block can move.
    bool DefragStep() {
        std::size_t offset = 0;
        while (offset + sizeof(BlockHeader) <= dataTop_) {
            BlockHeader* hdr = HeaderAt(offset);
            std::size_t nextOffset = offset + BlockTotal(hdr);

            if (hdr->free) {
                if (nextOffset >= dataTop_) {
                    dataTop_ = offset;
                    return true;
                }

                // Adjacent free blocks never exist, so the block after this one is in use.
                BlockHeader* nextHdr = HeaderAt(nextOffset);
                Entry* movedEntry = nextHdr->handleEntry;
                std::size_t movedSize = nextHdr->size;
                std::size_t nextBlockTotal = BlockTotal(nextHdr);
                // Read before the copy: dst is the free block's own header, and memmove overwrites it.
                std::size_t remainingFree = hdr->size;

                memmove(buffer_ + offset, buffer_ + nextOffset, nextBlockTotal);

                movedEntry->data = buffer_ + offset + sizeof(BlockHeader);

                std::size_t freeOffset = offset + sizeof(BlockHeader) + movedSize;
                BlockHeader* freeHdr = HeaderAt(freeOffset);
                freeHdr->handleEntry = nullptr;
                freeHdr->size = remainingFree;
                freeHdr->free = true;

                // The block after the moved one may be free. Merge it so no two free blocks are adjacent.
                MergeFreeBlock(freeOffset);
                return true;
            }

            offset = nextOffset;
        }
        return false;
    }

    std::size_t FragmentationPercent() const {
        std::size_t totalFree = 0;
        std::size_t freeBlocks = 0;
        std::size_t offset = 0;

        while (offset + sizeof(BlockHeader) <= dataTop_) {
            BlockHeader* hdr = reinterpret_cast<BlockHeader*>(buffer_ + offset);
            if (hdr->free) {
                totalFree += hdr->size;
                ++freeBlocks;
            }
            offset += sizeof(BlockHeader) + hdr->size;
        }

        if (totalFree == 0) return 0;
        if (dataTop_ == 0) return 0;
        return (totalFree * 100) / dataTop_;
    }

    std::size_t UsedBytes() const {
        std::size_t used = 0;
        std::size_t offset = 0;
        while (offset + sizeof(BlockHeader) <= dataTop_) {
            BlockHeader* hdr = reinterpret_cast<BlockHeader*>(buffer_ + offset);
            if (!hdr->free) {
                used += hdr->size;
            }
            offset += sizeof(BlockHeader) + hdr->size;
        }
        return used;
    }

    std::size_t TotalBytes() const {
        return capacity_;
    }

    std::size_t FreeBytes() const {
        std::size_t tableSz = entryCount_ * sizeof(Entry);
        std::size_t unallocated = capacity_ - dataTop_ - tableSz;

        std::size_t freeInBlocks = 0;
        std::size_t offset = 0;
        while (offset + sizeof(BlockHeader) <= dataTop_) {
            BlockHeader* hdr = reinterpret_cast<BlockHeader*>(buffer_ + offset);
            if (hdr->free) {
                freeInBlocks += hdr->size;
            }
            offset += sizeof(BlockHeader) + hdr->size;
        }

        return unallocated + freeInBlocks;
    }

private:
    // Block sizes are multiples of kBlockAlignment, so every BlockHeader lands on an aligned address. Entries sit at
    // multiples of their size from the end of the usable capacity, which is itself a multiple of kEntryAlignment.
    static constexpr std::size_t kBlockAlignment = 8;
    static constexpr std::size_t kEntryAlignment = alignof(Entry);
    static constexpr std::size_t NoBlock = static_cast<std::size_t>(-1);

    static constexpr std::size_t AlignUp(std::size_t value) {
        return (value + kBlockAlignment - 1) & ~(kBlockAlignment - 1);
    }

    static constexpr std::size_t AlignDown(std::size_t value, std::size_t alignment) {
        return value - value % alignment;
    }

    // Starts the heap at the first address that aligns entries. Bytes before that address are not used.
    void AdoptBuffer(uint8_t* buf, std::size_t size) {
        std::size_t misalignment = reinterpret_cast<std::uintptr_t>(buf) % kEntryAlignment;
        std::size_t skip = misalignment == 0 ? 0 : kEntryAlignment - misalignment;
        if (skip > size) {
            skip = size;
        }

        buffer_ = buf + skip;
        capacity_ = AlignDown(size - skip, kEntryAlignment);
    }


    // The caller holds the lock. Returns an invalid handle when no block of this size fits.
    Handle AllocateLocked(std::size_t size) {
        if (size > capacity_) {
            return Handle();
        }

        std::size_t aligned = AlignUp(size);
        Handle result = TryAllocateFromFreeBlock(aligned);
        if (result.IsValid()) {
            return result;
        }
        return AllocateAtTail(aligned);
    }


    // The caller holds the lock. The block goes after the used data. A freed entry slot is reused, so the entry
    // table does not grow with each allocation and free.
    Handle AllocateAtTail(std::size_t size) {
        Entry* entry = FindDeadEntry();
        bool reuse = entry != nullptr;
        std::size_t newEntryCount = reuse ? entryCount_ : entryCount_ + 1;
        std::size_t needed = dataTop_ + sizeof(BlockHeader) + size + newEntryCount * sizeof(Entry);
        if (needed > capacity_) {
            return Handle();
        }

        if (!reuse) {
            entry = EntrySlot(entryCount_);
            new (entry) Entry();
            entryCount_ = newEntryCount;
        }

        BlockHeader* hdr = HeaderAt(dataTop_);
        hdr->handleEntry = entry;
        hdr->size = size;
        hdr->free = false;

        void* data = buffer_ + dataTop_ + sizeof(BlockHeader);
        dataTop_ += sizeof(BlockHeader) + size;

        entry->Activate(data, size);
        return Handle(entry);
    }


    // The caller holds the lock. Frees the block of handle and clears handle. Other copies of the handle become invalid.
    void DeallocateLocked(Handle& handle) {
        Entry* entry = handle.GetEntry();
        if (entry == nullptr) {
            return;
        }

        std::size_t offset = HeaderOffset(entry->data);
        entry->Deactivate();
        HeaderAt(offset)->free = true;
        handle = Handle();
        MergeFreeBlock(offset);
    }


    // The caller holds the lock.
    Handle ReallocateLocked(Handle& handle, std::size_t newSize) {
        if (!handle.IsValid()) {
            return AllocateLocked(newSize);
        }

        std::size_t oldSize = handle.Size();
        if (newSize <= oldSize) {
            return handle;
        }

        Handle newHandle = AllocateLocked(newSize);
        if (!newHandle.IsValid()) {
            return Handle();
        }

        memcpy(newHandle.Data(), handle.Data(), oldSize);
        DeallocateLocked(handle);

        return newHandle;
    }


    BlockHeader* HeaderAt(std::size_t offset) const {
        return reinterpret_cast<BlockHeader*>(buffer_ + offset);
    }


    // Offset of the header of the block whose data starts at data.
    std::size_t HeaderOffset(const void* data) const {
        return static_cast<std::size_t>(static_cast<const uint8_t*>(data) - buffer_) - sizeof(BlockHeader);
    }


    static std::size_t BlockTotal(const BlockHeader* hdr) {
        return sizeof(BlockHeader) + hdr->size;
    }


    // Offset of the block that ends where the block at target starts, or NoBlock for the first block. Walks the
    // blocks from the start, so it costs one step per block before target.
    std::size_t PreviousBlockOffset(std::size_t target) const {
        std::size_t previous = NoBlock;
        std::size_t offset = 0;
        while (offset < target) {
            previous = offset;
            offset += BlockTotal(HeaderAt(offset));
        }
        return previous;
    }


    // Called for a block that has just become free. Merges it with a free block before it and with the free blocks
    // after it. A free block that ends the used region returns its space to the unallocated tail.
    void MergeFreeBlock(std::size_t offset) {
        std::size_t previous = PreviousBlockOffset(offset);
        if (previous != NoBlock && HeaderAt(previous)->free) {
            HeaderAt(previous)->size += BlockTotal(HeaderAt(offset));
            offset = previous;
        }

        BlockHeader* hdr = HeaderAt(offset);
        std::size_t next = offset + BlockTotal(hdr);
        while (next < dataTop_ && HeaderAt(next)->free) {
            hdr->size += BlockTotal(HeaderAt(next));
            next = offset + BlockTotal(hdr);
        }

        if (next >= dataTop_) {
            dataTop_ = offset;
        }
    }


    Entry* EntrySlot(std::size_t index) {
        return reinterpret_cast<Entry*>(
            buffer_ + capacity_ - (index + 1) * sizeof(Entry));
    }


    // A slot whose block was freed, or nullptr when every slot is in use. Reusing it keeps the table from growing.
    Entry* FindDeadEntry() {
        for (std::size_t i = 0; i < entryCount_; ++i) {
            Entry* e = EntrySlot(i);
            if (!e->alive) {
                return e;
            }
        }
        return nullptr;
    }


    Handle TryAllocateFromFreeBlock(std::size_t size) {
        std::size_t offset = 0;
        while (offset + sizeof(BlockHeader) <= dataTop_) {
            BlockHeader* hdr = reinterpret_cast<BlockHeader*>(buffer_ + offset);
            std::size_t blockTotal = sizeof(BlockHeader) + hdr->size;

            if (hdr->free && hdr->size >= size) {
                Entry* entry = FindDeadEntry();

                if (!entry) {
                    std::size_t newEntryCount = entryCount_ + 1;
                    std::size_t tableBytes = newEntryCount * sizeof(Entry);
                    if (dataTop_ + tableBytes > capacity_) {
                        offset += blockTotal;
                        continue;
                    }
                    entry = EntrySlot(entryCount_);
                    new (entry) Entry();
                    ++entryCount_;
                }

                std::size_t remaining = hdr->size - size;
                if (remaining > sizeof(BlockHeader) + 1) {
                    hdr->size = size;
                    hdr->free = false;
                    hdr->handleEntry = entry;

                    BlockHeader* splitHdr = reinterpret_cast<BlockHeader*>(
                        buffer_ + offset + sizeof(BlockHeader) + size);
                    splitHdr->handleEntry = nullptr;
                    splitHdr->size = remaining - sizeof(BlockHeader);
                    splitHdr->free = true;
                } else {
                    hdr->free = false;
                    hdr->handleEntry = entry;
                }

                void* data = buffer_ + offset + sizeof(BlockHeader);
                entry->Activate(data, hdr->size);

                return Handle(entry);
            }

            offset += blockTotal;
        }
        return Handle();
    }
};

}
