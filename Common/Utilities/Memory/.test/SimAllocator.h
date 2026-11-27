#pragma once

#include <cstddef>
#include <Utilities/Memory/IAllocator.h>

// Forwards every call to an inner allocator and counts the calls, so a test can see which allocator served a block.
class SimRecordingAllocator : public Memory::IAllocator {
private:
    Memory::IAllocator& inner_;
    int allocations_ = 0;
    int reallocations_ = 0;
    int deallocations_ = 0;

public:
    explicit SimRecordingAllocator(Memory::IAllocator& inner) : inner_(inner) {}

    int Allocations() const { return allocations_; }
    int Reallocations() const { return reallocations_; }
    int Deallocations() const { return deallocations_; }

    Memory::Handle Allocate(std::size_t size) override {
        allocations_++;
        return inner_.Allocate(size);
    }

    void Deallocate(Memory::Handle& handle) override {
        deallocations_++;
        inner_.Deallocate(handle);
    }

    Memory::Handle Reallocate(Memory::Handle& handle, std::size_t newSize) override {
        reallocations_++;
        return inner_.Reallocate(handle, newSize);
    }
};
