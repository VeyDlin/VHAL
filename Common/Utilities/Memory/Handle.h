#pragma once

#include <cstddef>
#include <cstdint>

namespace Memory {

// One slot of the entry table. The generation changes each time the slot is brought into use, so a Handle taken
// from an earlier use of the slot does not match it. The generation is 32 bits. A slot brought into use 2^32 times
// lets an old copy match again (see the README).
struct Entry {
    void*         data = nullptr;
    std::size_t   size = 0;
    std::uint32_t generation = 0;
    bool          alive = false;
    std::uint8_t  headerOffset = 0;   // bytes from data back to the block header

    // offset is the distance from blockData back to the header of a heap block. A pool block has no header.
    void Activate(void* blockData, std::size_t blockSize, std::uint8_t offset = 0) {
        data = blockData;
        size = blockSize;
        headerOffset = offset;
        generation++;
        alive = true;
    }


    // Marks the slot free. Only the allocator that owns the block calls this, when it frees the block.
    void Deactivate() {
        data = nullptr;
        size = 0;
        alive = false;
    }
};

class Handle {
private:
    Entry*        entry_;
    std::uint32_t generation_;

public:
    Handle() : entry_(nullptr), generation_(0) {}
    explicit Handle(Entry* e) : entry_(e), generation_(e != nullptr ? e->generation : 0) {}


    // False for an empty handle, for a handle whose block was freed, and for a copy whose slot has been reused.
    bool IsValid() const {
        return entry_ != nullptr && entry_->alive && entry_->generation == generation_;
    }


    void* Data() const {
        if (!IsValid()) {
            return nullptr;
        }
        return entry_->data;
    }


    std::size_t Size() const {
        if (!IsValid()) {
            return 0;
        }
        return entry_->size;
    }


    // Clears this copy only. The block stays allocated and other copies stay valid. Deallocate frees a block.
    void Invalidate() {
        entry_ = nullptr;
        generation_ = 0;
    }


    // The slot of a valid handle, or nullptr when the handle is not valid.
    Entry* GetEntry() const {
        if (!IsValid()) {
            return nullptr;
        }
        return entry_;
    }
};

}
