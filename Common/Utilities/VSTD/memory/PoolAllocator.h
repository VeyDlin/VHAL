#pragma once

#include <cstdint>
#include <cstddef>
#include <new>
#include "IAllocator.h"

namespace VSTD {

// BlockSize is the minimum usable size of a block. Blocks are laid out at a stride
// rounded up to pointer alignment so the free list can live inside every free block.
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

	static constexpr std::size_t kAlignment = alignof(FreeNode);
	static constexpr std::size_t kStride = (BlockSize + kAlignment - 1) & ~(kAlignment - 1);

	alignas(kAlignment) uint8_t pool_[kStride * BlockCount] = {};
    BlockEntry entries_[BlockCount] = {};
    FreeNode* freeList_ = nullptr;
    std::size_t allocatedCount_ = 0;

public:
    PoolAllocator() {
        for (std::size_t i = BlockCount; i > 0; i--) {
			FreeNode* node = reinterpret_cast<FreeNode*>(pool_ + (i - 1) * kStride);
            node->next = freeList_;
            freeList_ = node;
        }
    }

    Handle Allocate(std::size_t size) override {
        if (size > BlockSize || !freeList_) return Handle();

        FreeNode* node = freeList_;
        freeList_ = node->next;

		std::size_t blockIndex = (reinterpret_cast<uint8_t*>(node) - pool_) / kStride;
        BlockEntry& be = entries_[blockIndex];
        be.handleEntry.Activate(node, size);
        be.blockIndex = blockIndex;

        allocatedCount_++;
        return Handle(&be.handleEntry);
    }

    void Deallocate(Handle& handle) override {
        if (!handle.IsValid()) return;

        void* data = handle.Data();
		std::size_t blockIndex = (static_cast<uint8_t*>(data) - pool_) / kStride;
        if (blockIndex >= BlockCount) return;

        FreeNode* node = reinterpret_cast<FreeNode*>(data);
        node->next = freeList_;
        freeList_ = node;

        handle.GetEntry()->Deactivate();
        handle = Handle{};
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
