#pragma once

#include <cstdint>
#include <cstddef>
#include <new>
#include "IAllocator.h"

namespace VSTD {

template<std::size_t BlockSize, std::size_t BlockCount>
class PoolAllocator : public IAllocator {
    static_assert(BlockSize >= sizeof(void*), "BlockSize must be at least pointer-sized");

private:
    struct FreeNode {
        FreeNode* next;
    };

    struct BlockEntry {
        Entry handleEntry;
        std::size_t blockIndex;
    };

    alignas(8) uint8_t pool_[BlockSize * BlockCount] = {};
    BlockEntry entries_[BlockCount] = {};
    FreeNode* freeList_ = nullptr;
    std::size_t allocatedCount_ = 0;

public:
    PoolAllocator() {
        for (std::size_t i = BlockCount; i > 0; i--) {
            FreeNode* node = reinterpret_cast<FreeNode*>(pool_ + (i - 1) * BlockSize);
            node->next = freeList_;
            freeList_ = node;
        }
    }

    Handle Allocate(std::size_t size) override {
        if (size > BlockSize || !freeList_) return Handle();

        FreeNode* node = freeList_;
        freeList_ = node->next;

        std::size_t blockIndex = (reinterpret_cast<uint8_t*>(node) - pool_) / BlockSize;
        BlockEntry& be = entries_[blockIndex];
        be.handleEntry.data = node;
        be.handleEntry.size = size;
        be.handleEntry.alive = true;
        be.blockIndex = blockIndex;

        allocatedCount_++;
        return Handle(&be.handleEntry);
    }

    void Deallocate(Handle& handle) override {
        if (!handle.IsValid()) return;

        void* data = handle.Data();
        std::size_t blockIndex = (static_cast<uint8_t*>(data) - pool_) / BlockSize;
        if (blockIndex >= BlockCount) return;

        FreeNode* node = reinterpret_cast<FreeNode*>(data);
        node->next = freeList_;
        freeList_ = node;

        handle.Invalidate();
        allocatedCount_--;
    }

    Handle Reallocate(Handle& handle, std::size_t newSize) override {
        if (!handle.IsValid()) {
            return Allocate(newSize);
        }
        if (newSize <= BlockSize) {
            handle.GetEntry()->size = newSize;
            return handle;
        }
        return Handle();
    }

    std::size_t AllocatedCount() const { return allocatedCount_; }
    std::size_t FreeCount() const { return BlockCount - allocatedCount_; }
    std::size_t TotalBlocks() const { return BlockCount; }

    constexpr std::size_t GetBlockSize() const { return BlockSize; }
};

}
