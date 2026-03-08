#pragma once

#include <type_traits>

namespace VSTD {

template<typename T>
constexpr typename std::remove_reference<T>::type&& Move(T&& value) noexcept {
    return static_cast<typename std::remove_reference<T>::type&&>(value);
}

template<typename T>
constexpr T&& Forward(typename std::remove_reference<T>::type& value) noexcept {
    return static_cast<T&&>(value);
}

template<typename T>
constexpr T&& Forward(typename std::remove_reference<T>::type&& value) noexcept {
    return static_cast<T&&>(value);
}

template<typename T>
constexpr void Swap(T& a, T& b) noexcept {
    T tmp = Move(a);
    a = Move(b);
    b = Move(tmp);
}

}
