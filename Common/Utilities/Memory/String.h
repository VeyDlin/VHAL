#pragma once

#include <Utilities/Memory/Vector.h>
#include <cstdint>
#include <string.h>
#include <string_view>

namespace Memory {

// Operations that can allocate return false when the allocator refuses memory, and then leave the string unchanged
// (same text, still terminated). Constructors and operator+ cannot return a status: they yield an empty string
// on failure, so use the checked member functions where the difference matters.
template<typename AllocatorType = IAllocator>
class String {
public:
    static constexpr std::size_t Npos = static_cast<std::size_t>(-1);

private:
    Vector<char, AllocatorType> data_;

public:
    String() = default;

    String(const char* str) {
        SetText(str);
    }

    String(const char* str, std::type_identity_t<AllocatorType>* alloc) : data_(alloc) {
        SetText(str);
    }

    explicit String(std::type_identity_t<AllocatorType>* alloc) : data_(alloc) {}

    String(const String& other) : data_(other.data_) {}
    String(String&& other) noexcept : data_(static_cast<Vector<char, AllocatorType>&&>(other.data_)) {}

    // Cannot report failure: use Assign for a checked copy. On failure the old text is kept.
    String& operator=(const String& other) {
        Assign(other);
        return *this;
    }

    String& operator=(String&& other) noexcept {
        data_ = static_cast<Vector<char, AllocatorType>&&>(other.data_);
        return *this;
    }

    bool Assign(const String& other) {
        return data_.Assign(other.data_);
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


    bool Append(const char* str) {
        if (!str) return true;
        std::size_t len = strlen(str);
        if (len == 0) return true;

        // str may point into our own buffer, which growth frees: append from a private copy instead.
        if (IsInOwnBuffer(str)) {
            String copy(str);
            if (copy.Length() != len) return false;
            return Append(copy.Data());
        }

        if (!ReserveFor(Length() + len)) return false;
        PopTerminator();
        for (std::size_t i = 0; i < len; i++) {
            data_.PushBack(str[i]);
        }
        data_.PushBack('\0');
        return true;
    }

    bool Append(char c) {
        if (!ReserveFor(Length() + 1)) return false;
        PopTerminator();
        data_.PushBack(c);
        data_.PushBack('\0');
        return true;
    }

    bool Append(const String& other) {
        return Append(other.Data());
    }

    void Clear() {
        data_.Clear();
    }

    // Returns false for an index past the end, and when the allocator refuses memory.
    bool Insert(std::size_t index, const char* str) {
        if (!str) return true;
        std::size_t insertLen = strlen(str);
        if (insertLen == 0) return true;
        std::size_t len = Length();
        if (index > len) return false;

        if (IsInOwnBuffer(str)) {
            String copy(str);
            if (copy.Length() != insertLen) return false;
            return Insert(index, copy.Data());
        }

        if (!ReserveFor(len + insertLen)) return false;
        EnsureTerminator();
        for (std::size_t i = 0; i < insertLen; i++) {
            data_.Insert(index + i, str[i]);
        }
        return true;
    }

    bool Insert(std::size_t index, char c) {
        std::size_t len = Length();
        if (index > len) return false;
        if (!ReserveFor(len + 1)) return false;
        EnsureTerminator();
        return data_.Insert(index, c);
    }

    void Erase(std::size_t index, std::size_t count = 1) {
        std::size_t len = Length();
        if (index >= len || count == 0) return;
        if (count > len - index) count = len - index;
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

    // Stores the substring into out. Returns false, with out unchanged, when the allocator refuses memory.
    bool Substr(std::size_t pos, String& out, std::size_t count = Npos) const {
        std::size_t len = Length();
        if (pos >= len) {
            out.Clear();
            return true;
        }
        if (count == Npos || count > len - pos) count = len - pos;

        String result;
        if (!result.ReserveFor(count)) return false;
        const char* d = Data();
        for (std::size_t i = 0; i < count; i++) {
            result.data_.PushBack(d[pos + i]);
        }
        result.data_.PushBack('\0');
        out.data_ = static_cast<Vector<char, AllocatorType>&&>(result.data_);
        return true;
    }

    std::string_view ToView() const {
        return std::string_view(Data(), Length());
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

    // Returns an empty string when the allocator refuses memory; use Append for a checked concatenation.
    String operator+(const String& other) const {
        String result(*this);
        if (result.Length() != Length() || !result.Append(other)) return String();
        return result;
    }

    String operator+(const char* str) const {
        String result(*this);
        if (result.Length() != Length() || !result.Append(str)) return String();
        return result;
    }

private:
    bool IsInOwnBuffer(const char* str) const {
        if (data_.Empty()) return false;
        std::uintptr_t begin = reinterpret_cast<std::uintptr_t>(data_.Data());
        std::uintptr_t address = reinterpret_cast<std::uintptr_t>(str);
        return address >= begin && address < begin + data_.Size();
    }

    // Capacity for a string of contentLength characters plus its terminator. Nothing else changes.
    bool ReserveFor(std::size_t contentLength) {
        return data_.Reserve(contentLength + 1);
    }

    // Only called with capacity already reserved, so none of these pushes can allocate.
    void PopTerminator() {
        char* last = data_.Back();
        if (last != nullptr && *last == '\0') {
            data_.PopBack();
        }
    }

    void EnsureTerminator() {
        if (data_.Empty()) {
            data_.PushBack('\0');
        }
    }

    // Used by the constructors on a string that is still empty: a refused allocation leaves it empty.
    bool SetText(const char* str) {
        if (!str) return true;
        std::size_t len = strlen(str);
        if (!ReserveFor(len)) return false;
        for (std::size_t i = 0; i < len; i++) {
            data_.PushBack(str[i]);
        }
        data_.PushBack('\0');
        return true;
    }
};

}
