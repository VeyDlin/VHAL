#pragma once

#include <cstddef>

namespace VSTD {

template<typename T>
class Span {
public:
    using ValueType = T;
    using Iterator = T*;
    using ConstIterator = const T*;

private:
    T* data_ = nullptr;
    std::size_t size_ = 0;

public:
    constexpr Span() = default;
    constexpr Span(T* data, std::size_t size) : data_(data), size_(size) {}

    template<std::size_t N>
    constexpr Span(T (&arr)[N]) : data_(arr), size_(N) {}

    template<typename Container>
    constexpr Span(Container& c) : data_(c.Data()), size_(c.Size()) {}


    constexpr std::size_t Size() const { return size_; }
    constexpr bool Empty() const { return size_ == 0; }
    constexpr T* Data() const { return data_; }
    constexpr T& operator[](std::size_t i) const { return data_[i]; }
    constexpr T& Front() const { return data_[0]; }
    constexpr T& Back() const { return data_[size_ - 1]; }

    constexpr Span Subspan(std::size_t offset, std::size_t count) const {
        return Span(data_ + offset, count);
    }

    constexpr Span First(std::size_t count) const {
        return Span(data_, count);
    }

    constexpr Span Last(std::size_t count) const {
        return Span(data_ + size_ - count, count);
    }

    constexpr Iterator begin() const { return data_; }
    constexpr Iterator end() const { return data_ + size_; }
};

}
