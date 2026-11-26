#pragma once

#include <Utilities/Memory/Handle.h>

namespace Memory {

// Type-erased allocator interface, for code that takes any heap at run time (the network clients). A heap is used
// through its template type; only code that needs this interface pays for the virtual calls.
class IAllocator {
public:
    virtual ~IAllocator() = default;
    virtual Handle Allocate(std::size_t size) = 0;
    virtual void Deallocate(Handle& handle) = 0;
    virtual Handle Reallocate(Handle& handle, std::size_t newSize) = 0;
};


// Presents a heap of type HeapType as an IAllocator. The heap must outlive the adapter.
template <typename HeapType>
class AllocatorOf : public IAllocator {
private:
    HeapType& heap_;

public:
    explicit AllocatorOf(HeapType& heap)
        : heap_(heap) {
    }

    Handle Allocate(std::size_t size) override {
        return heap_.Allocate(size);
    }

    void Deallocate(Handle& handle) override {
        heap_.Deallocate(handle);
    }

    Handle Reallocate(Handle& handle, std::size_t newSize) override {
        return heap_.Reallocate(handle, newSize);
    }
};

}
