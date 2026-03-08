#pragma once

#include <cstddef>
#include <new>
#include <type_traits>

#include "../core/Move.h"

namespace VSTD {

struct NulloptT {
    explicit constexpr NulloptT(int) {}
};

inline constexpr NulloptT Nullopt{0};

template <typename T>
class Optional {
private:
    alignas(T) unsigned char storage_[sizeof(T)];
    bool hasValue_;

public:

    Optional() : hasValue_(false) {}

    Optional(NulloptT) : hasValue_(false) {}

    Optional(const T& value) : hasValue_(false) {
        Construct(value);
    }

    Optional(T&& value) : hasValue_(false) {
        Construct(Move(value));
    }

    Optional(const Optional& other) : hasValue_(false) {
        if (other.hasValue_) {
            Construct(other.Get());
        }
    }

    Optional(Optional&& other) : hasValue_(false) {
        if (other.hasValue_) {
            Construct(Move(other.Get()));
            other.Reset();
        }
    }

    ~Optional() {
        Reset();
    }

    Optional& operator=(NulloptT) {
        Reset();
        return *this;
    }

    Optional& operator=(const T& value) {
        if (hasValue_) {
            Get() = value;
        } else {
            Construct(value);
        }
        return *this;
    }

    Optional& operator=(T&& value) {
        if (hasValue_) {
            Get() = Move(value);
        } else {
            Construct(Move(value));
        }
        return *this;
    }

    Optional& operator=(const Optional& other) {
        if (this != &other) {
            if (other.hasValue_) {
                if (hasValue_) {
                    Get() = other.Get();
                } else {
                    Construct(other.Get());
                }
            } else {
                Reset();
            }
        }
        return *this;
    }

    Optional& operator=(Optional&& other) {
        if (this != &other) {
            if (other.hasValue_) {
                if (hasValue_) {
                    Get() = Move(other.Get());
                } else {
                    Construct(Move(other.Get()));
                }
                other.Reset();
            } else {
                Reset();
            }
        }
        return *this;
    }

    bool HasValue() const { return hasValue_; }

    explicit operator bool() const { return hasValue_; }

    T& Value() { return Get(); }
    const T& Value() const { return Get(); }

    T ValueOr(const T& defaultValue) const {
        return hasValue_ ? Get() : defaultValue;
    }

    T& operator*() { return Get(); }
    const T& operator*() const { return Get(); }

    T* operator->() { return &Get(); }
    const T* operator->() const { return &Get(); }

    void Reset() {
        if (hasValue_) {
            Get().~T();
            hasValue_ = false;
        }
    }

    template <typename... Args>
    T& Emplace(Args&&... args) {
        Reset();
        T* ptr = new (&storage_) T(static_cast<Args&&>(args)...);
        hasValue_ = true;
        return *ptr;
    }

private:
    void Construct(const T& value) {
        new (&storage_) T(value);
        hasValue_ = true;
    }

    void Construct(T&& value) {
        new (&storage_) T(Move(value));
        hasValue_ = true;
    }

    T& Get() {
        return *reinterpret_cast<T*>(&storage_);
    }

    const T& Get() const {
        return *reinterpret_cast<const T*>(&storage_);
    }
};

}
