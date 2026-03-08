#pragma once

#include "../memory/Handle.h"
#include "../memory/IAllocator.h"
#include "../memory/DefaultHeap.h"
#include <cstddef>
#include <new>
#include <type_traits>

namespace VSTD {

template<typename T>
class UniquePtr {
private:
    Handle handle_;
    IAllocator* allocator_ = nullptr;

public:
    UniquePtr() = default;

    UniquePtr(Handle handle, IAllocator* alloc)
        : handle_(handle), allocator_(alloc) {}


    ~UniquePtr() {
        Reset();
    }


    // Non-copyable
    UniquePtr(const UniquePtr&) = delete;
    UniquePtr& operator=(const UniquePtr&) = delete;

    // Move constructor
    UniquePtr(UniquePtr&& other) noexcept
        : handle_(other.handle_), allocator_(other.allocator_) {
        other.handle_ = Handle{};
        other.allocator_ = nullptr;
    }

    // Move assignment
    UniquePtr& operator=(UniquePtr&& other) noexcept {
        if (this != &other) {
            Reset();
            handle_ = other.handle_;
            allocator_ = other.allocator_;
            other.handle_ = Handle{};
            other.allocator_ = nullptr;
        }
        return *this;
    }


    T& operator*() const { return *static_cast<T*>(handle_.Data()); }
    T* operator->() const { return static_cast<T*>(handle_.Data()); }

    T* Get() const {
        if (!handle_.IsValid()) return nullptr;
        return static_cast<T*>(handle_.Data());
    }

    explicit operator bool() const { return handle_.IsValid(); }


    void Reset() {
        if (handle_.IsValid()) {
            static_cast<T*>(handle_.Data())->~T();
            auto* alloc = GetAlloc();
            if (alloc) alloc->Deallocate(handle_);
        }
        handle_ = Handle{};
    }

    Handle Release() {
        Handle h = handle_;
        handle_ = Handle{};
        return h;
    }

private:
    IAllocator* GetAlloc() const {
        return allocator_ ? allocator_ : GetDefaultHeap();
    }
};

template<typename T, typename... Args>
UniquePtr<T> MakeUnique(Args&&... args) {
    IAllocator* alloc = GetDefaultHeap();
    Handle handle = alloc->Allocate(sizeof(T));
    if (!handle.IsValid()) return UniquePtr<T>{};
    new (handle.Data()) T(static_cast<Args&&>(args)...);
    return UniquePtr<T>(handle, alloc);
}

template<typename T, typename... Args>
UniquePtr<T> MakeUnique(IAllocator* alloc, Args&&... args) {
    Handle handle = alloc->Allocate(sizeof(T));
    if (!handle.IsValid()) return UniquePtr<T>{};
    new (handle.Data()) T(static_cast<Args&&>(args)...);
    return UniquePtr<T>(handle, alloc);
}

}
