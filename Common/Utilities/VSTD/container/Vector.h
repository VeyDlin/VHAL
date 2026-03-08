#pragma once

#include "../memory/Handle.h"
#include "../memory/IAllocator.h"
#include "../memory/DefaultHeap.h"
#include <cstddef>
#include <string.h>
#include <new>
#include <type_traits>
#include <utility>

namespace VSTD {

template<typename T>
class Vector {
public:
    using ValueType = T;
    using Iterator = T*;
    using ConstIterator = const T*;

private:
    Handle handle_;
    std::size_t size_ = 0;
    std::size_t capacity_ = 0;
    IAllocator* allocator_ = nullptr;

public:
    Vector() = default;

    explicit Vector(IAllocator* alloc) : allocator_(alloc) {}

    ~Vector() {
        DestroyElements();
        if (handle_.IsValid()) {
            auto* alloc = GetAlloc();
            if (alloc) alloc->Deallocate(handle_);
        }
    }


    // Copy constructor
    Vector(const Vector& other) : allocator_(other.allocator_) {
        if (other.size_ > 0) {
            auto* alloc = GetAlloc();
            handle_ = alloc->Allocate(other.size_ * sizeof(T));
            if (handle_.IsValid()) {
                capacity_ = other.size_;
                T* dst = RawData();
                const T* src = other.RawData();
                for (std::size_t i = 0; i < other.size_; i++) {
                    new (&dst[i]) T(src[i]);
                }
                size_ = other.size_;
            }
        }
    }

    // Move constructor
    Vector(Vector&& other) noexcept
        : handle_(other.handle_), size_(other.size_),
          capacity_(other.capacity_), allocator_(other.allocator_) {
        other.handle_ = Handle{};
        other.size_ = 0;
        other.capacity_ = 0;
    }

    // Copy assignment
    Vector& operator=(const Vector& other) {
        if (this != &other) {
            DestroyElements();
            if (handle_.IsValid()) {
                auto* alloc = GetAlloc();
                if (alloc) alloc->Deallocate(handle_);
                handle_ = Handle{};
            }
            capacity_ = 0;
            allocator_ = other.allocator_;

            if (other.size_ > 0) {
                auto* alloc = GetAlloc();
                handle_ = alloc->Allocate(other.size_ * sizeof(T));
                if (handle_.IsValid()) {
                    capacity_ = other.size_;
                    T* dst = RawData();
                    const T* src = other.RawData();
                    for (std::size_t i = 0; i < other.size_; i++) {
                        new (&dst[i]) T(src[i]);
                    }
                    size_ = other.size_;
                }
            }
        }
        return *this;
    }

    // Move assignment
    Vector& operator=(Vector&& other) noexcept {
        if (this != &other) {
            DestroyElements();
            if (handle_.IsValid()) {
                auto* alloc = GetAlloc();
                if (alloc) alloc->Deallocate(handle_);
            }
            handle_ = other.handle_;
            size_ = other.size_;
            capacity_ = other.capacity_;
            allocator_ = other.allocator_;
            other.handle_ = Handle{};
            other.size_ = 0;
            other.capacity_ = 0;
        }
        return *this;
    }


    void PushBack(const T& value) {
        if (size_ >= capacity_) Grow(size_ + 1);
        T* data = RawData();
        if (data) {
            new (&data[size_]) T(value);
            size_++;
        }
    }

    void PushBack(T&& value) {
        if (size_ >= capacity_) Grow(size_ + 1);
        T* data = RawData();
        if (data) {
            new (&data[size_]) T(static_cast<T&&>(value));
            size_++;
        }
    }

    template<typename... Args>
    T& EmplaceBack(Args&&... args) {
        if (size_ >= capacity_) Grow(size_ + 1);
        T* data = RawData();
        T* ptr = new (&data[size_]) T(static_cast<Args&&>(args)...);
        size_++;
        return *ptr;
    }

    void PopBack() {
        if (size_ > 0) {
            size_--;
            if constexpr (!std::is_trivially_destructible_v<T>) {
                RawData()[size_].~T();
            }
        }
    }

    void Insert(std::size_t index, const T& value) {
        if (index > size_) return;
        if (size_ >= capacity_) Grow(size_ + 1);
        T* data = RawData();
        for (std::size_t i = size_; i > index; i--) {
            new (&data[i]) T(static_cast<T&&>(data[i - 1]));
            data[i - 1].~T();
        }
        new (&data[index]) T(value);
        size_++;
    }

    void Erase(std::size_t index) {
        if (index >= size_) return;
        T* data = RawData();
        data[index].~T();
        for (std::size_t i = index; i + 1 < size_; i++) {
            new (&data[i]) T(static_cast<T&&>(data[i + 1]));
            data[i + 1].~T();
        }
        size_--;
    }

    void EraseRange(std::size_t first, std::size_t count) {
        if (first >= size_ || count == 0) return;
        if (first + count > size_) count = size_ - first;
        T* data = RawData();
        for (std::size_t i = first; i < first + count; i++) {
            data[i].~T();
        }
        for (std::size_t i = first; i + count < size_; i++) {
            new (&data[i]) T(static_cast<T&&>(data[i + count]));
            data[i + count].~T();
        }
        size_ -= count;
    }


    std::size_t Find(const T& value) const {
        const T* data = RawData();
        for (std::size_t i = 0; i < size_; i++) {
            if (data[i] == value) return i;
        }
        return static_cast<std::size_t>(-1);
    }

    bool Contains(const T& value) const {
        return Find(value) != static_cast<std::size_t>(-1);
    }


    T& operator[](std::size_t i) { return RawData()[i]; }
    const T& operator[](std::size_t i) const { return RawData()[i]; }

    T& Front() { return RawData()[0]; }
    const T& Front() const { return RawData()[0]; }
    T& Back() { return RawData()[size_ - 1]; }
    const T& Back() const { return RawData()[size_ - 1]; }

    T* Data() { return RawData(); }
    const T* Data() const { return RawData(); }

    std::size_t Size() const { return size_; }
    std::size_t Capacity() const { return capacity_; }
    bool Empty() const { return size_ == 0; }


    void Clear() {
        DestroyElements();
    }

    void Reserve(std::size_t newCapacity) {
        if (newCapacity > capacity_) {
            Grow(newCapacity);
        }
    }

    void Resize(std::size_t newSize) {
        if (newSize < size_) {
            if constexpr (!std::is_trivially_destructible_v<T>) {
                T* data = RawData();
                for (std::size_t i = newSize; i < size_; i++) {
                    data[i].~T();
                }
            }
            size_ = newSize;
        } else if (newSize > size_) {
            Reserve(newSize);
            T* data = RawData();
            for (std::size_t i = size_; i < newSize; i++) {
                new (&data[i]) T();
            }
            size_ = newSize;
        }
    }


    Iterator begin() { return RawData(); }
    Iterator end() { return RawData() + size_; }
    ConstIterator begin() const { return RawData(); }
    ConstIterator end() const { return RawData() + size_; }

private:
    IAllocator* GetAlloc() const {
        return allocator_ ? allocator_ : GetDefaultHeap();
    }

    T* RawData() const {
        if (!handle_.IsValid()) return nullptr;
        return static_cast<T*>(handle_.Data());
    }

    void Grow(std::size_t minCapacity) {
        std::size_t newCap = capacity_ == 0 ? 4 : capacity_ * 2;
        if (newCap < minCapacity) newCap = minCapacity;

        auto* alloc = GetAlloc();
        Handle newHandle = alloc->Reallocate(handle_, newCap * sizeof(T));
        if (!newHandle.IsValid()) return;

        handle_ = newHandle;
        capacity_ = newCap;
    }

    void DestroyElements() {
        if constexpr (!std::is_trivially_destructible_v<T>) {
            T* data = RawData();
            if (data) {
                for (std::size_t i = 0; i < size_; i++) {
                    data[i].~T();
                }
            }
        }
        size_ = 0;
    }
};

}
