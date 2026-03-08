#pragma once

#include "Vector.h"
#include "StringView.h"
#include <string.h>

namespace VSTD {

class String {
public:
    static constexpr std::size_t Npos = static_cast<std::size_t>(-1);

private:
    Vector<char> data_;

public:
    String() = default;

    String(const char* str) {
        if (str) {
            std::size_t len = strlen(str);
            data_.Reserve(len + 1);
            for (std::size_t i = 0; i < len; i++) {
                data_.PushBack(str[i]);
            }
            data_.PushBack('\0');
        }
    }

    String(const char* str, IAllocator* alloc) : data_(alloc) {
        if (str) {
            std::size_t len = strlen(str);
            data_.Reserve(len + 1);
            for (std::size_t i = 0; i < len; i++) {
                data_.PushBack(str[i]);
            }
            data_.PushBack('\0');
        }
    }

    explicit String(IAllocator* alloc) : data_(alloc) {}

    String(const String& other) : data_(other.data_) {}
    String(String&& other) noexcept : data_(static_cast<Vector<char>&&>(other.data_)) {}

    String& operator=(const String& other) {
        data_ = other.data_;
        return *this;
    }

    String& operator=(String&& other) noexcept {
        data_ = static_cast<Vector<char>&&>(other.data_);
        return *this;
    }


    std::size_t Length() const {
        if (data_.Empty()) return 0;
        return data_.Size() - 1;
    }

    bool Empty() const { return Length() == 0; }

    const char* Data() const {
        if (data_.Empty()) return "";
        return data_.Data();
    }

    char& operator[](std::size_t i) { return data_[i]; }
    const char& operator[](std::size_t i) const { return data_[i]; }


    void Append(const char* str) {
        if (!str) return;
        std::size_t len = strlen(str);
        if (len == 0) return;

        if (!data_.Empty() && data_.Back() == '\0') {
            data_.PopBack();
        }

        for (std::size_t i = 0; i < len; i++) {
            data_.PushBack(str[i]);
        }
        data_.PushBack('\0');
    }

    void Append(char c) {
        if (!data_.Empty() && data_.Back() == '\0') {
            data_.PopBack();
        }
        data_.PushBack(c);
        data_.PushBack('\0');
    }

    void Append(const String& other) {
        Append(other.Data());
    }

    void Clear() {
        data_.Clear();
    }

    void Insert(std::size_t index, const char* str) {
        if (!str) return;
        std::size_t insertLen = strlen(str);
        if (insertLen == 0) return;
        std::size_t len = Length();
        if (index > len) return;

        EnsureNullTerminated();
        for (std::size_t i = 0; i < insertLen; i++) {
            data_.Insert(index + i, str[i]);
        }
    }

    void Insert(std::size_t index, char c) {
        std::size_t len = Length();
        if (index > len) return;
        EnsureNullTerminated();
        data_.Insert(index, c);
    }

    void Erase(std::size_t index, std::size_t count = 1) {
        std::size_t len = Length();
        if (index >= len || count == 0) return;
        if (index + count > len) count = len - index;
        data_.EraseRange(index, count);
    }


    std::size_t Find(char c, std::size_t from = 0) const {
        std::size_t len = Length();
        const char* d = Data();
        for (std::size_t i = from; i < len; i++) {
            if (d[i] == c) return i;
        }
        return Npos;
    }

    std::size_t Find(const char* str, std::size_t from = 0) const {
        if (!str) return Npos;
        std::size_t subLen = strlen(str);
        if (subLen == 0) return from;
        std::size_t len = Length();
        if (subLen > len) return Npos;
        const char* d = Data();
        for (std::size_t i = from; i + subLen <= len; i++) {
            if (memcmp(d + i, str, subLen) == 0) return i;
        }
        return Npos;
    }

    bool Contains(char c) const {
        return Find(c) != Npos;
    }

    bool Contains(const char* str) const {
        return Find(str) != Npos;
    }

    bool StartsWith(const char* prefix) const {
        if (!prefix) return true;
        std::size_t prefixLen = strlen(prefix);
        if (prefixLen > Length()) return false;
        return memcmp(Data(), prefix, prefixLen) == 0;
    }

    bool EndsWith(const char* suffix) const {
        if (!suffix) return true;
        std::size_t suffixLen = strlen(suffix);
        std::size_t len = Length();
        if (suffixLen > len) return false;
        return memcmp(Data() + len - suffixLen, suffix, suffixLen) == 0;
    }

    String Substr(std::size_t pos, std::size_t count = Npos) const {
        std::size_t len = Length();
        if (pos >= len) return String();
        if (count == Npos || pos + count > len) count = len - pos;
        String result;
        result.data_.Reserve(count + 1);
        const char* d = Data();
        for (std::size_t i = 0; i < count; i++) {
            result.data_.PushBack(d[pos + i]);
        }
        result.data_.PushBack('\0');
        return result;
    }

    StringView ToView() const {
        return StringView(Data(), Length());
    }


    bool operator==(const String& other) const {
        if (Length() != other.Length()) return false;
        return memcmp(Data(), other.Data(), Length()) == 0;
    }

    bool operator!=(const String& other) const {
        return !(*this == other);
    }

    bool operator==(const char* str) const {
        if (!str) return Empty();
        return strcmp(Data(), str) == 0;
    }

    bool operator!=(const char* str) const {
        return !(*this == str);
    }

    String operator+(const String& other) const {
        String result(*this);
        result.Append(other);
        return result;
    }

    String operator+(const char* str) const {
        String result(*this);
        result.Append(str);
        return result;
    }

private:
    void EnsureNullTerminated() {
        if (data_.Empty() || data_.Back() != '\0') {
            data_.PushBack('\0');
        }
    }
};

}
