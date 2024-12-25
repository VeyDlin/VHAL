#pragma once
#include <string_view>
#include <type_traits>
#include <cstring>
#include <cstdio> 

class Print {
public:
    enum class Format { Hex = 16, Dec = 10, Oct = 8, Bin = 2 };

    virtual ~Print() = default;

    virtual void WriteRaw(const char* data, size_t size) = 0;

    void Line(const char* newline = "\r\n") {
        WriteRaw(newline, strlen(newline));
    }

    template <typename T>
    std::enable_if_t<std::is_integral_v<T>, size_t>
    Write(T number, Format format = Format::Dec) {
        return WriteNumber(number, format);
    }

    template <typename T>
    std::enable_if_t<std::is_floating_point_v<T>, size_t>
    Write(T number, uint8_t precision = 2) {
        return WriteFloat(number, precision);
    }

    size_t Write(const std::string_view str) {
        WriteRaw(str.data(), str.size());
        return str.size();
    }

    size_t WriteLn(const std::string_view str) {
        auto len = Write(str);
        Line();
        return len + 2; // \r\n
    }

    size_t Write(bool value) {
        return Write(value ? "true" : "false");
    }

    size_t WriteLn(bool value) {
        return WriteLn(value ? "true" : "false");
    }

    size_t Write(uint8_t* buffer, size_t size) {
        WriteRaw(reinterpret_cast<const char*>(buffer), size);
        return size;
    }

private:
    template <typename T>
    size_t WriteNumber(T number, Format format) {
        char buffer[64] = {0};
        if (format == Format::Bin) {
            size_t idx = 0;
            for (int i = sizeof(T) * 8 - 1; i >= 0; --i) {
                buffer[idx++] = (number & (1 << i)) ? '1' : '0';
            }
            buffer[idx] = '\0';
        } else {
            const char* fmt = (format == Format::Hex) ? "%X" :
                              (format == Format::Oct) ? "%o" : "%d";
            snprintf(buffer, sizeof(buffer), fmt, number);
        }
        return Write(buffer);
    }

    template <typename T>
    size_t WriteFloat(T number, uint8_t precision) {
        char buffer[64] = {0};
        snprintf(buffer, sizeof(buffer), "%.*f", precision, number);
        return Write(buffer);
    }
};