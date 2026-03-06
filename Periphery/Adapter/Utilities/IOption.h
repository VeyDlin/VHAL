#pragma once
#include <System/System.h>


template<typename CodeType>
struct IOption {
protected:
    CodeType value{};

public:
    constexpr IOption() noexcept = default;
    constexpr explicit IOption(CodeType value) noexcept
        : value(value) { }

    constexpr CodeType Get() const noexcept {
        return value;
    }

    constexpr bool operator==(const IOption& other) const noexcept {
        return value == other.value;
    }

    constexpr bool operator!=(const IOption& other) const noexcept {
        return value != other.value;
    }

    constexpr bool operator==(CodeType value) const noexcept {
        return this->value == value;
    }

    constexpr bool operator!=(CodeType value) const noexcept {
        return this->value != value;
    }

    constexpr operator CodeType() const noexcept {
        return this->value;
    }
};



template<typename CodeType>
struct IOptionDouble {
protected:
    CodeType value1{};
    CodeType value2{};

public:
    constexpr IOptionDouble() noexcept = default;

    constexpr explicit IOptionDouble(CodeType value1, CodeType value2) noexcept
        : value1(value1), value2(value2) { }

    template<uint8 Index>
    constexpr CodeType Get() const noexcept {
        static_assert(Index == 1 || Index == 2, "Index must be 1 or 2");
        if constexpr (Index == 1) {
            return value1;
        } else {
            return value2;
        }
    }

    constexpr bool operator==(const IOptionDouble& other) const noexcept {
        return value1 == other.value1 && value2 == other.value2;
    }

    constexpr bool operator!=(const IOptionDouble& other) const noexcept {
        return !(*this == other);
    }
};



template<typename CodeType>
struct IOptionFlag {
protected:
    CodeType value{};

public:
    constexpr IOptionFlag() noexcept = default;
    constexpr IOptionFlag(CodeType value) noexcept
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
};
