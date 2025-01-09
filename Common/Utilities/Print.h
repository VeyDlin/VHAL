#pragma once
#include <System/System.h>
#include <string_view>
#include <type_traits>
#include <cstring>
#include <cstdio> 
#include <cstddef>


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
    std::enable_if_t<std::is_integral_v<T>, size_t>
    WriteLine(T number, Format format = Format::Dec) {
        auto len = Write(number, format);
        Line();
        return len + 2; // \r\n
    }


    template <typename T>
    std::enable_if_t<std::is_floating_point_v<T>, size_t>
    Write(T number, uint8 precision = 2) {
        return WriteFloat(number, precision);
    }

    template <typename T>
    std::enable_if_t<std::is_floating_point_v<T>, size_t>
    WriteLine(T number, uint8 precision = 2) {
        auto len = Write(number, precision);
        Line();
        return len + 2; // \r\n
    }


    size_t Write(const std::string_view str) {
        WriteRaw(str.data(), str.size());
        return str.size();
    }

    size_t WriteLine(const std::string_view str) {
        auto len = Write(str);
        Line();
        return len + 2; // \r\n
    }


    size_t Write(bool value) {
        return Write(std::string_view(value ? "true" : "false"));
    }

    size_t WriteLine(bool value) {
        return WriteLine(std::string_view(value ? "true" : "false"));
    }


    size_t Write(uint8* buffer, size_t size) {
        WriteRaw(reinterpret_cast<const char*>(buffer), size);
        return size;
    }

    size_t WriteLine(uint8* buffer, size_t size) {
        auto len = Write(buffer, size);
        Line();
        return len + 2; // \r\n
    }


private:
    template <typename T>
    size_t WriteNumber(T number, Format format) {
        char buffer[64] = {0};
        size_t idx = 0;

        if (format == Format::Bin) {
            for (int i = sizeof(T) * 8 - 1; i >= 0; --i) {
                buffer[idx++] = (number & (1 << i)) ? '1' : '0';
            }
        } else if (format == Format::Hex) {
            static const char* hex_digits = "0123456789ABCDEF";
            do {
                buffer[idx++] = hex_digits[number % 16];
                number /= 16;
            } while (number > 0);
        } else if (format == Format::Oct) {
            do {
                buffer[idx++] = '0' + (number % 8);
                number /= 8;
            } while (number > 0);
        } else { // Format::Dec
            bool is_negative = (number < 0);
            if (is_negative) {
                number = -number;
            }
            do {
                buffer[idx++] = '0' + (number % 10);
                number /= 10;
            } while (number > 0);
            if (is_negative) {
                buffer[idx++] = '-';
            }
        }

        for (size_t i = 0; i < idx / 2; ++i) {
            std::swap(buffer[i], buffer[idx - i - 1]);
        }

        buffer[idx] = '\0';
        return Write(buffer);
    }


    template <typename T>
    size_t WriteFloat(T number, uint8 precision) {
        char buffer[64] = {0};
        size_t idx = 0;

        if (number < 0) {
            buffer[idx++] = '-';
            number = -number;
        }

        T integer_part = static_cast<T>(number);
        T fractional_part = number - integer_part;

        idx += WriteNumber(integer_part, Format::Dec);

        if (precision > 0) {
            buffer[idx++] = '.';
        }

        for (uint8 i = 0; i < precision; ++i) {
            fractional_part *= 10;
            int digit = static_cast<int>(fractional_part);
            buffer[idx++] = '0' + digit;
            fractional_part -= digit;
        }

        buffer[idx] = '\0';
        return Write(buffer);
    }
};
