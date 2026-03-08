#pragma once
#include <cstddef>

namespace VSTD {

template<typename T, std::size_t N>
class Array {
    T data_[N];
public:
    using ValueType = T;
    using Iterator = T*;
    using ConstIterator = const T*;

    constexpr std::size_t Size() const { return N; }
    constexpr bool Empty() const { return N == 0; }
    T& operator[](std::size_t i) { return data_[i]; }
    const T& operator[](std::size_t i) const { return data_[i]; }
    T& Front() { return data_[0]; }
    const T& Front() const { return data_[0]; }
    T& Back() { return data_[N - 1]; }
    const T& Back() const { return data_[N - 1]; }
    T* Data() { return data_; }
    const T* Data() const { return data_; }
    void Fill(const T& value) { for (std::size_t i = 0; i < N; i++) data_[i] = value; }
    Iterator begin() { return data_; }
    Iterator end() { return data_ + N; }
    ConstIterator begin() const { return data_; }
    ConstIterator end() const { return data_ + N; }
};

template<typename T>
class Array<T, 0> {
public:
    using ValueType = T;
    using Iterator = T*;
    using ConstIterator = const T*;
    constexpr std::size_t Size() const { return 0; }
    constexpr bool Empty() const { return true; }
    T* Data() { return nullptr; }
    const T* Data() const { return nullptr; }
    void Fill(const T&) {}
    Iterator begin() { return nullptr; }
    Iterator end() { return nullptr; }
    ConstIterator begin() const { return nullptr; }
    ConstIterator end() const { return nullptr; }
};

}
