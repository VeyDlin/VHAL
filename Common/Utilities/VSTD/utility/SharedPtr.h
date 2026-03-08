#pragma once

#include "../memory/Handle.h"
#include "../memory/IAllocator.h"
#include "../memory/DefaultHeap.h"
#include <cstddef>
#include <new>
#include <type_traits>

namespace VSTD {

template<typename T>
class SharedPtr {
private:
    struct ControlBlock {
        T value;
        std::size_t refCount;

        template<typename... Args>
        ControlBlock(Args&&... args)
            : value(static_cast<Args&&>(args)...), refCount(1) {}
    };

    Handle handle_;
    IAllocator* allocator_ = nullptr;

public:
    SharedPtr() = default;

    explicit SharedPtr(IAllocator* alloc) : allocator_(alloc) {}

    SharedPtr(Handle handle, IAllocator* alloc)
        : handle_(handle), allocator_(alloc) {}

    ~SharedPtr() {
        Release();
    }

    SharedPtr(const SharedPtr& other)
        : handle_(other.handle_), allocator_(other.allocator_) {
        ControlBlock* block = GetBlock();
        if (block) {
            block->refCount++;
        }
    }

    SharedPtr(SharedPtr&& other) noexcept
        : handle_(other.handle_), allocator_(other.allocator_) {
        other.handle_ = Handle{};
    }

    SharedPtr& operator=(const SharedPtr& other) {
        if (this != &other) {
            Release();
            handle_ = other.handle_;
            allocator_ = other.allocator_;
            ControlBlock* block = GetBlock();
            if (block) {
                block->refCount++;
            }
        }
        return *this;
    }

    SharedPtr& operator=(SharedPtr&& other) noexcept {
        if (this != &other) {
            Release();
            handle_ = other.handle_;
            allocator_ = other.allocator_;
            other.handle_ = Handle{};
        }
        return *this;
    }

    T* Get() const {
        ControlBlock* block = GetBlock();
        return block ? &block->value : nullptr;
    }

    T& operator*() const { return *Get(); }
    T* operator->() const { return Get(); }

    explicit operator bool() const { return GetBlock() != nullptr; }

    std::size_t UseCount() const {
        ControlBlock* block = GetBlock();
        return block ? block->refCount : 0;
    }

    bool IsUnique() const { return UseCount() == 1; }

    void Reset() {
        Release();
    }

    bool operator==(const SharedPtr& other) const {
        return Get() == other.Get();
    }

    bool operator!=(const SharedPtr& other) const {
        return Get() != other.Get();
    }

    template<typename U, typename... Args>
    friend SharedPtr<U> MakeShared(Args&&... args);

    template<typename U, typename... Args>
    friend SharedPtr<U> AllocateShared(IAllocator* alloc, Args&&... args);

private:
    IAllocator* GetAlloc() const {
        return allocator_ ? allocator_ : GetDefaultHeap();
    }

    ControlBlock* GetBlock() const {
        if (!handle_.IsValid()) return nullptr;
        return static_cast<ControlBlock*>(handle_.Data());
    }

    void Release() {
        ControlBlock* block = GetBlock();
        if (block) {
            block->refCount--;
            if (block->refCount == 0) {
                block->~ControlBlock();
                auto* alloc = GetAlloc();
                if (alloc) alloc->Deallocate(handle_);
            }
        }
        handle_ = Handle{};
    }
};

template<typename T, typename... Args>
SharedPtr<T> MakeShared(Args&&... args) {
    auto* alloc = GetDefaultHeap();
    Handle handle = alloc->Allocate(sizeof(typename SharedPtr<T>::ControlBlock));
    if (!handle.IsValid()) return SharedPtr<T>();

    using CB = typename SharedPtr<T>::ControlBlock;
    new (handle.Data()) CB(static_cast<Args&&>(args)...);

    return SharedPtr<T>(handle, alloc);
}

template<typename T, typename... Args>
SharedPtr<T> AllocateShared(IAllocator* alloc, Args&&... args) {
    Handle handle = alloc->Allocate(sizeof(typename SharedPtr<T>::ControlBlock));
    if (!handle.IsValid()) return SharedPtr<T>(alloc);

    using CB = typename SharedPtr<T>::ControlBlock;
    new (handle.Data()) CB(static_cast<Args&&>(args)...);

    return SharedPtr<T>(handle, alloc);
}

}
