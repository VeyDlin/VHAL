#pragma once
#include <VHAL.h>
#include <Utilities/TextNumber/TextNumber.h>
#include <span>
#include <string_view>
#include <type_traits>
#include <cstring>
#include <cstdio>
#include <cstddef>
#include <cmath>
#include <limits>
#include <algorithm>


class Print {
public:
    enum class Format { Hex = 16, Dec = 10, Oct = 8, Bin = 2 };

    virtual ~Print() = default;

    virtual void WriteRaw(const char* data, size_t size) = 0;

    size_t Line(const char* newline = "\r\n") {
    	size_t size = strlen(newline);
        WriteRaw(newline, size);
        return size;
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
        return len + Line();
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
        return len + Line();
    }


    size_t Write(const std::string_view str) {
        WriteRaw(str.data(), str.size());
        return str.size();
    }


    size_t WriteLine(const std::string_view str) {
        auto len = Write(str);
        return len + Line();
    }


    inline size_t Write(const char* value) {
    	return Write(std::string_view(value));
    }


    inline size_t WriteLine(const char* value) {
        return WriteLine(std::string_view(value));
    }


    inline size_t Write(const char* value, size_t size) {
    	return Write(std::string_view(value, size));
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
        return len + Line();
    }


	size_t WriteBuffer(uint8* buffer, size_t size) {
		for (size_t i = 0; i < size; i++) {
	        if ((buffer[i] & 0xF0) == 0) {
	            Write("0");
	        }

			Write(buffer[i], Print::Format::Hex);
			Write(" ");
		}
		return size;
	}


	size_t WriteBufferLine(uint8* buffer, size_t size) {
        auto len = WriteBuffer(buffer, size);
        return len + Line();
	}


    // Writes the number into buffer, truncated to fit and always terminated with NUL. Returns the length without the NUL.
    template <typename T>
    static std::enable_if_t<std::is_integral_v<T>, size_t>
    NumberToString(char* buffer, size_t bufferSize, T number, Format format = Format::Dec) {
        if (bufferSize == 0) {
            return 0;
        }

        // Print has always written nothing for a lone zero in decimal into a one-byte buffer; that is kept.
        if (bufferSize == 1 && number == 0 && format == Format::Dec) {
            return 0;
        }

        char text[66];
        size_t length = FormatInteger<T>(std::span<char>(text, sizeof(text)), number, ToNumberBase(format));
        return CopyText(buffer, bufferSize, text, length);
    }


    // Writes the number into buffer with the same truncation as NumberToString. NaN and infinity are not truncated: they are
    // written whole or not at all, and 0 is returned when they do not fit.
    template <typename T>
    static std::enable_if_t<std::is_floating_point_v<T>, size_t>
    FloatToString(char* buffer, size_t bufferSize, T number, uint8_t precision = 2) {
        if (bufferSize == 0) {
            return 0;
        }

        char text[64];
        double value = static_cast<double>(number);
        size_t length = FormatFloat<double>(std::span<char>(text, sizeof(text)), value, precision);
        if (value != value || std::isinf(value)) {
            if (length >= bufferSize) {
                return 0;
            }
        }

        return CopyText(buffer, bufferSize, text, length);
    }


    // Reads an integer from the start of str (blanks and a '+' are allowed). result is changed only on success.
    // endPos, when given, receives the position where the reading stopped.
    template <typename T>
    static std::enable_if_t<std::is_integral_v<T>, bool>
    StringToNumber(const char* str, T& result, size_t* endPos = nullptr) {
        if (!str) {
            return false;
        }

        // bool is read as one byte, as Print always did: a value up to 255 is accepted and is true when it is not zero
        using Parsed = std::conditional_t<std::is_same_v<T, bool>, uint8_t, T>;
        std::string_view text(str);
        IntegerOptions<Parsed> options = IntegerOptions<Parsed>{}.WithPlusSign().WithLeadingBlanks();
        NumberResult<Parsed> parsed = ParseInteger<Parsed>(text, options);
        size_t stop = parsed.endPosition;
        if (parsed.error.code == NumberErrorCode::OutOfRange) {
            stop = IntegerStopPosition<Parsed>(text, options, parsed.endPosition);
        }

        if (endPos) {
            *endPos = stop;
        }

        if (parsed.IsOk()) {
            result = static_cast<T>(parsed.value);
        }

        return parsed.IsOk();
    }


    // Reads a float from the start of str (blanks and a '+' are allowed). result is changed only on success.
    // endPos, when given, receives the position where the reading stopped.
    template <typename T>
    static std::enable_if_t<std::is_floating_point_v<T>, bool>
    StringToFloat(const char* str, T& result, size_t* endPos = nullptr) {
        if (!str) {
            return false;
        }

        // float is read as float, so it is rounded once; every other type is read as double and then converted
        using Parsed = std::conditional_t<std::is_same_v<T, float>, float, double>;
        std::string_view text(str);
        FloatOptions options = FloatOptions{}.WithPlusSign().WithLeadingBlanks();
        NumberResult<Parsed> parsed = ParseFloat<Parsed>(text, options);
        if (endPos) {
            *endPos = FloatStopPosition(text, parsed.IsOk(), parsed.endPosition);
        }

        if (parsed.IsOk()) {
            result = static_cast<T>(parsed.value);
        }

        return parsed.IsOk();
    }


private:
    static constexpr NumberBase ToNumberBase(Format format) {
        if (format == Format::Hex) {
            return NumberBase::Hexadecimal;
        }

        if (format == Format::Oct) {
            return NumberBase::Octal;
        }

        if (format == Format::Bin) {
            return NumberBase::Binary;
        }

        return NumberBase::Decimal;
    }


    static bool IsBlank(char character) {
        return character == ' ' || character == '\t';
    }


    // The first letter of "inf" or "nan" in either case
    static bool StartsSpecialWord(char character) {
        return character == 'i' || character == 'I' || character == 'n' || character == 'N';
    }


    // Print stops an integer at the digit that overflows, but the library reads on to the end of the accepted text. The
    // same place is the longest prefix that is still in range, because the magnitude only grows as digits are added.
    template <typename T>
    static size_t IntegerStopPosition(std::string_view text, IntegerOptions<T> options, size_t acceptedEnd) {
        for (size_t length = acceptedEnd; length > 0; --length) {
            if (ParseInteger<T>(text.substr(0, length), options).IsOk()) {
                return length;
            }
        }

        return 0;
    }


    // Where Print stops a float, where that differs from the library. A partial "inf" or "nan" is not read at all, so a
    // failure stops after the blanks and the sign. A number that is good and ends in an exponent marker with no digit reads
    // that marker and one sign after it, unless an exponent was read already.
    static size_t FloatStopPosition(std::string_view text, bool ok, size_t acceptedEnd) {
        size_t bodyStart = 0;
        while (bodyStart < text.size() && IsBlank(text[bodyStart])) {
            bodyStart++;
        }

        if (bodyStart < text.size() && (text[bodyStart] == '+' || text[bodyStart] == '-')) {
            bodyStart++;
        }

        if (bodyStart < text.size() && StartsSpecialWord(text[bodyStart])) {
            return ok ? acceptedEnd : bodyStart;
        }

        if (!ok || text.substr(0, acceptedEnd).find_first_of("eE") != std::string_view::npos) {
            return acceptedEnd;
        }

        size_t stop = acceptedEnd;
        if (stop < text.size() && (text[stop] == 'e' || text[stop] == 'E')) {
            stop++;
            if (stop < text.size() && (text[stop] == '+' || text[stop] == '-')) {
                stop++;
            }
        }

        return stop;
    }


    // Copies text into buffer, keeping one byte for the terminator, and returns the number of characters copied
    static size_t CopyText(char* buffer, size_t bufferSize, const char* text, size_t length) {
        size_t copyLength = length < bufferSize - 1 ? length : bufferSize - 1;
        for (size_t index = 0; index < copyLength; ++index) {
            buffer[index] = text[index];
        }

        buffer[copyLength] = '\0';
        return copyLength;
    }


    template <typename T>
    std::enable_if_t<std::is_integral_v<T>, size_t>
    WriteNumber(T number, Format format) {
        char buffer[66];
        size_t len = NumberToString(buffer, sizeof(buffer), number, format);
        if (len > 0) {
            WriteRaw(buffer, len);
        }
        return len;
    }


    template <typename T>
    std::enable_if_t<std::is_floating_point_v<T>, size_t>
    WriteFloat(T number, uint8 precision) {
        char buffer[64];
        size_t len = FloatToString(buffer, sizeof(buffer), number, precision);
        if (len > 0) {
            WriteRaw(buffer, len);
        }
        return len;
    }

};
