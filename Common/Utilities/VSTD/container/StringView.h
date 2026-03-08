#pragma once

#include <cstddef>
#include <string.h>

namespace VSTD {

class StringView {
public:
    static constexpr std::size_t Npos = static_cast<std::size_t>(-1);

private:
    const char* data_ = nullptr;
    std::size_t size_ = 0;

public:
    constexpr StringView() = default;
    constexpr StringView(const char* str, std::size_t len) : data_(str), size_(len) {}

    StringView(const char* str) : data_(str), size_(str ? strlen(str) : 0) {}


    constexpr std::size_t Length() const { return size_; }
    constexpr std::size_t Size() const { return size_; }
    constexpr bool Empty() const { return size_ == 0; }
    constexpr const char* Data() const { return data_; }

    constexpr char operator[](std::size_t i) const { return data_[i]; }
    constexpr char Front() const { return data_[0]; }
    constexpr char Back() const { return data_[size_ - 1]; }

    constexpr StringView Substr(std::size_t pos, std::size_t count) const {
        if (pos >= size_) return StringView();
        if (count > size_ - pos) count = size_ - pos;
        return StringView(data_ + pos, count);
    }

    constexpr StringView First(std::size_t count) const {
        return Substr(0, count);
    }

    constexpr StringView Last(std::size_t count) const {
        if (count > size_) count = size_;
        return StringView(data_ + size_ - count, count);
    }

    bool StartsWith(StringView prefix) const {
        if (prefix.size_ > size_) return false;
        return memcmp(data_, prefix.data_, prefix.size_) == 0;
    }

    bool EndsWith(StringView suffix) const {
        if (suffix.size_ > size_) return false;
        return memcmp(data_ + size_ - suffix.size_, suffix.data_, suffix.size_) == 0;
    }

    bool Contains(char c) const {
        for (std::size_t i = 0; i < size_; i++) {
            if (data_[i] == c) return true;
        }
        return false;
    }

    std::size_t Find(char c, std::size_t from = 0) const {
        for (std::size_t i = from; i < size_; i++) {
            if (data_[i] == c) return i;
        }
        return Npos;
    }

    std::size_t Find(StringView needle, std::size_t from = 0) const {
        if (needle.size_ == 0) return from;
        if (needle.size_ > size_) return Npos;
        for (std::size_t i = from; i <= size_ - needle.size_; i++) {
            if (memcmp(data_ + i, needle.data_, needle.size_) == 0) return i;
        }
        return Npos;
    }

    void RemovePrefix(std::size_t n) {
        if (n > size_) n = size_;
        data_ += n;
        size_ -= n;
    }

    void RemoveSuffix(std::size_t n) {
        if (n > size_) n = size_;
        size_ -= n;
    }

    bool operator==(StringView other) const {
        if (size_ != other.size_) return false;
        if (size_ == 0) return true;
        return memcmp(data_, other.data_, size_) == 0;
    }

    bool operator!=(StringView other) const { return !(*this == other); }

    bool operator==(const char* str) const {
        return *this == StringView(str);
    }

    bool operator!=(const char* str) const { return !(*this == str); }

    const char* begin() const { return data_; }
    const char* end() const { return data_ + size_; }
};

}
