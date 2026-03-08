#pragma once

#include "Handle.h"

namespace VSTD {

// Type-erased allocator interface so containers can work
// with any Heap<DefragPolicy, LockPolicy> instantiation.
class IAllocator {
public:
    virtual ~IAllocator() = default;
    virtual Handle Allocate(std::size_t size) = 0;
    virtual void Deallocate(Handle& handle) = 0;
    virtual Handle Reallocate(Handle& handle, std::size_t newSize) = 0;
};

}
