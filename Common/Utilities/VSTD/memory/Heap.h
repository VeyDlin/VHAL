#pragma once

#include <cstdint>
#include <cstddef>
#include <string.h>
#include <new>
#include "IAllocator.h"

namespace VSTD {

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
    // External buffer, pointer + size
    Heap(uint8_t* buf, std::size_t size)
        : buffer_(buf), capacity_(size), ownsBuffer_(false),
          dataTop_(0), entryCount_(0) {}

    // External buffer, array reference
    template <std::size_t N>
    explicit Heap(uint8_t (&buf)[N])
        : Heap(buf, N) {}

    // Internal buffer
    explicit Heap(std::size_t size)
        : buffer_(new uint8_t[size]), capacity_(size), ownsBuffer_(true),
          dataTop_(0), entryCount_(0) {}

    ~Heap() {
        if (ownsBuffer_) delete[] buffer_;
    }

    // Non-copyable
    Heap(const Heap&) = delete;
    Heap& operator=(const Heap&) = delete;

    Handle Allocate(std::size_t size) override {
        LockPolicy::Lock();

        Handle result = TryAllocateFromFreeBlock(size);
        if (result.IsValid()) {
            LockPolicy::Unlock();
            return result;
        }

        std::size_t newEntryCount = entryCount_ + 1;
        std::size_t tableBytes = newEntryCount * sizeof(Entry);
        std::size_t needed = dataTop_ + sizeof(BlockHeader) + size + tableBytes;

        if (needed > capacity_) {
            LockPolicy::Unlock();
            if (oomCallback_) oomCallback_();
            return Handle();
        }

        Entry* entry = EntrySlot(entryCount_);
        new (entry) Entry();
        entryCount_ = newEntryCount;

        BlockHeader* hdr = reinterpret_cast<BlockHeader*>(buffer_ + dataTop_);
        hdr->handleEntry = entry;
        hdr->size = size;
        hdr->free = false;

        void* data = buffer_ + dataTop_ + sizeof(BlockHeader);
        dataTop_ += sizeof(BlockHeader) + size;

        entry->data = data;
        entry->size = size;
        entry->alive = true;

        LockPolicy::Unlock();
        return Handle(entry);
    }

    void Deallocate(Handle& handle) override {
        LockPolicy::Lock();
        if (handle.IsValid()) {
            void* data = handle.Data();
            BlockHeader* hdr = reinterpret_cast<BlockHeader*>(
                static_cast<uint8_t*>(data) - sizeof(BlockHeader));
            hdr->free = true;
            handle.Invalidate();
        }
        LockPolicy::Unlock();
    }

    Handle Reallocate(Handle& handle, std::size_t newSize) override {
        if (!handle.IsValid()) {
            return Allocate(newSize);
        }

        std::size_t oldSize = handle.Size();
        if (newSize <= oldSize) {
            return handle;
        }

        Handle newHandle = Allocate(newSize);
        if (!newHandle.IsValid()) {
            return Handle{};
        }

        memcpy(newHandle.Data(), handle.Data(), oldSize);
        Deallocate(handle);

        return newHandle;
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

    bool DefragStep() {
        std::size_t offset = 0;
        while (offset + sizeof(BlockHeader) <= dataTop_) {
            BlockHeader* hdr = reinterpret_cast<BlockHeader*>(buffer_ + offset);
            std::size_t blockTotal = sizeof(BlockHeader) + hdr->size;

            if (hdr->free) {
                std::size_t nextOffset = offset + blockTotal;

                if (nextOffset >= dataTop_) {
                    dataTop_ = offset;
                    return true;
                }

                BlockHeader* nextHdr = reinterpret_cast<BlockHeader*>(buffer_ + nextOffset);

                if (nextHdr->free) {
                    hdr->size += sizeof(BlockHeader) + nextHdr->size;
                    return true;
                }

                std::size_t nextBlockTotal = sizeof(BlockHeader) + nextHdr->size;
                uint8_t* src = buffer_ + nextOffset;
                uint8_t* dst = buffer_ + offset;

                Entry* movedEntry = nextHdr->handleEntry;
                std::size_t movedSize = nextHdr->size;

                memmove(dst, src, nextBlockTotal);

                BlockHeader* movedHdr = reinterpret_cast<BlockHeader*>(dst);
                void* newData = dst + sizeof(BlockHeader);
                movedEntry->data = newData;

                std::size_t remainingFree = hdr->size;
                BlockHeader* freeHdr = reinterpret_cast<BlockHeader*>(
                    dst + sizeof(BlockHeader) + movedSize);
                freeHdr->handleEntry = nullptr;
                freeHdr->size = remainingFree;
                freeHdr->free = true;

                return true;
            }

            offset += blockTotal;
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
    Entry* EntrySlot(std::size_t index) {
        return reinterpret_cast<Entry*>(
            buffer_ + capacity_ - (index + 1) * sizeof(Entry));
    }

    Handle TryAllocateFromFreeBlock(std::size_t size) {
        std::size_t offset = 0;
        while (offset + sizeof(BlockHeader) <= dataTop_) {
            BlockHeader* hdr = reinterpret_cast<BlockHeader*>(buffer_ + offset);
            std::size_t blockTotal = sizeof(BlockHeader) + hdr->size;

            if (hdr->free && hdr->size >= size) {
                Entry* entry = nullptr;
                for (std::size_t i = 0; i < entryCount_; ++i) {
                    Entry* e = EntrySlot(i);
                    if (!e->alive) {
                        entry = e;
                        break;
                    }
                }

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
                entry->data = data;
                entry->size = hdr->size;
                entry->alive = true;

                return Handle(entry);
            }

            offset += blockTotal;
        }
        return Handle();
    }
};

}
