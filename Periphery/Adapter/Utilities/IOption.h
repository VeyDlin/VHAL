#pragma once
#include <System/System.h>


// Одиночная опция
template<typename CodeType>
struct IOption {
    constexpr IOption() noexcept = default;
    constexpr explicit IOption(CodeType value) noexcept
        : _value(value) { }

    constexpr CodeType Get() const noexcept {
        return _value;
    }

    constexpr bool operator==(const IOption& other) const noexcept {
        return _value == other._value;
    }

    constexpr bool operator!=(const IOption& other) const noexcept {
        return _value != other._value;
    }

    constexpr bool operator==(CodeType value) const noexcept {
        return _value == value;
    }

    constexpr bool operator!=(CodeType value) const noexcept {
        return _value != value;
    }

    constexpr operator CodeType() const noexcept {
        return _value;
    }

protected:
    CodeType _value{};
};



template<typename CodeType>
struct IOptionDouble {
    constexpr IOptionDouble() noexcept = default;

    constexpr explicit IOptionDouble(CodeType value1, CodeType value2) noexcept
        : _value1(value1), _value2(value2) { }

    template<uint8 Index>
    constexpr CodeType Get() const noexcept {
        static_assert(Index == 1 || Index == 2, "Index must be 1 or 2");
        if constexpr (Index == 1) return _value1;
        else                      return _value2;
    }

    constexpr bool operator==(const IOptionDouble& other) const noexcept {
        return _value1 == other._value1 && _value2 == other._value2;
    }

    constexpr bool operator!=(const IOptionDouble& other) const noexcept {
        return !(*this == other);
    }

protected:
    CodeType _value1{};
    CodeType _value2{};
};



template<typename CodeType>
struct IOptionFlag {
    constexpr IOptionFlag() noexcept = default;
    constexpr explicit IOptionFlag(CodeType value) noexcept
        : value(value) { }

    constexpr CodeType Get() const noexcept {
        return value;
    }

    constexpr IOptionFlag operator|(const IOptionFlag& other) const noexcept {
        return IOptionFlag(static_cast<CodeType>(value | other.value));
    }

    constexpr IOptionFlag& operator|=(const IOptionFlag& other) noexcept {
        value = static_cast<CodeType>(value | other.value);
        return *this;
    }

    constexpr IOptionFlag operator&(const IOptionFlag& other) const noexcept {
        return IOptionFlag(static_cast<CodeType>(value & other.value));
    }

    constexpr bool Has(const IOptionFlag& flag) const noexcept {
        return (value & flag.value) == flag.value;
    }

    constexpr bool operator==(const IOptionFlag& other) const noexcept {
        return value == other.value;
    }

    constexpr bool operator!=(const IOptionFlag& other) const noexcept {
        return value != other.value;
    }

    constexpr operator CodeType() const noexcept {
        return value;
    }

protected:
    CodeType value{};
};
