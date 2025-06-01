#pragma once
#include <System/System.h>
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


public:
    // Static number to string conversion methods
    template <typename T>
    static std::enable_if_t<std::is_integral_v<T>, size_t>
    NumberToString(char* buffer, size_t bufferSize, T number, Format format = Format::Dec) {
        if (bufferSize == 0) return 0;
        
        size_t idx = 0;
        
        // Handle zero case
        if (number == 0 && format == Format::Dec) {
            if (bufferSize > 1) {
                buffer[0] = '0';
                buffer[1] = '\0';
                return 1;
            }
            return 0;
        }

        char tempBuffer[66]; // Max for binary representation of 64-bit number + null
        
        if (format == Format::Bin) {
            size_t bits = sizeof(T) * 8;
            for (size_t i = 0; i < bits && idx < sizeof(tempBuffer) - 1; ++i) {
                tempBuffer[idx++] = (number & (static_cast<T>(1) << (bits - 1 - i))) ? '1' : '0';
            }
        } else if (format == Format::Hex) {
            static const char* hex_digits = "0123456789ABCDEF";
            using UnsignedT = std::make_unsigned_t<T>;
            UnsignedT unum = static_cast<UnsignedT>(number);
            do {
                tempBuffer[idx++] = hex_digits[unum % 16];
                unum /= 16;
            } while (unum > 0 && idx < sizeof(tempBuffer) - 1);
            // Reverse hex digits
            for (size_t i = 0; i < idx / 2; ++i) {
                std::swap(tempBuffer[i], tempBuffer[idx - i - 1]);
            }
        } else if (format == Format::Oct) {
            using UnsignedT = std::make_unsigned_t<T>;
            UnsignedT unum = static_cast<UnsignedT>(number);
            do {
                tempBuffer[idx++] = '0' + (unum % 8);
                unum /= 8;
            } while (unum > 0 && idx < sizeof(tempBuffer) - 1);
            // Reverse oct digits
            for (size_t i = 0; i < idx / 2; ++i) {
                std::swap(tempBuffer[i], tempBuffer[idx - i - 1]);
            }
        } else { // Format::Dec
            bool is_negative = false;
            using UnsignedT = std::make_unsigned_t<T>;
            UnsignedT unum;
            
            if constexpr (std::is_signed_v<T>) {
                if (number < 0) {
                    is_negative = true;
                    unum = static_cast<UnsignedT>(-static_cast<std::make_signed_t<T>>(number));
                } else {
                    unum = static_cast<UnsignedT>(number);
                }
            } else {
                unum = number;
            }
            
            size_t num_start = idx;
            do {
                tempBuffer[idx++] = '0' + (unum % 10);
                unum /= 10;
            } while (unum > 0 && idx < sizeof(tempBuffer) - 1);
            
            // Reverse digits
            for (size_t i = num_start; i < num_start + (idx - num_start) / 2; ++i) {
                std::swap(tempBuffer[i], tempBuffer[idx - (i - num_start) - 1]);
            }
            
            if (is_negative && idx < sizeof(tempBuffer) - 1) {
                // Insert minus at the beginning
                for (size_t i = idx; i > 0; --i) {
                    tempBuffer[i] = tempBuffer[i - 1];
                }
                tempBuffer[0] = '-';
                idx++;
            }
        }

        // Copy to output buffer
        size_t copyLen = (idx < bufferSize - 1) ? idx : bufferSize - 1;
        std::memcpy(buffer, tempBuffer, copyLen);
        buffer[copyLen] = '\0';
        return copyLen;
    }
    
    
    // Static float/double to string conversion
    template <typename T>
    static std::enable_if_t<std::is_floating_point_v<T>, size_t>
    FloatToString(char* buffer, size_t bufferSize, T number, uint8 precision = 2) {
        if (bufferSize == 0) return 0;
        
        char tempBuffer[64];
        size_t idx = 0;

        if (number < 0) {
            tempBuffer[idx++] = '-';
            number = -number;
        }

        // Handle special cases
        if (std::isnan(number)) {
            const char* nan = "NaN";
            size_t len = std::strlen(nan);
            if (len < bufferSize) {
                std::strcpy(buffer, nan);
                return len;
            }
            return 0;
        }
        
        if (std::isinf(number)) {
            const char* inf = (tempBuffer[0] == '-') ? "-Inf" : "Inf";
            size_t len = std::strlen(inf);
            if (len < bufferSize) {
                std::strcpy(buffer, inf);
                return len;
            }
            return 0;
        }

        T integer_part = std::floor(number);
        T fractional_part = number - integer_part;

        // Convert integer part
        auto int_part = static_cast<uint64_t>(integer_part);
        size_t int_start = idx;
        if (int_part == 0) {
            tempBuffer[idx++] = '0';
        } else {
            size_t num_start = idx;
            while (int_part > 0 && idx < sizeof(tempBuffer) - 1) {
                tempBuffer[idx++] = '0' + (int_part % 10);
                int_part /= 10;
            }
            // Reverse integer digits
            for (size_t i = num_start; i < num_start + (idx - num_start) / 2; ++i) {
                std::swap(tempBuffer[i], tempBuffer[idx - (i - num_start) - 1]);
            }
        }

        // Add decimal point and fractional part
        if (precision > 0 && idx < sizeof(tempBuffer) - 1) {
            tempBuffer[idx++] = '.';
            for (uint8 i = 0; i < precision && idx < sizeof(tempBuffer) - 1; ++i) {
                fractional_part *= 10;
                int digit = static_cast<int>(fractional_part);
                tempBuffer[idx++] = '0' + digit;
                fractional_part -= digit;
            }
        }

        // Copy to output buffer
        size_t copyLen = (idx < bufferSize - 1) ? idx : bufferSize - 1;
        std::memcpy(buffer, tempBuffer, copyLen);
        buffer[copyLen] = '\0';
        return copyLen;
    }
    
    
    // Static string to number conversion methods
    template <typename T>
    static std::enable_if_t<std::is_integral_v<T>, bool>
    StringToNumber(const char* str, T& result, size_t* endPos = nullptr) {
        if (!str) return false;
        
        size_t pos = 0;
        result = 0;
        
        // Skip whitespace
        while (str[pos] == ' ' || str[pos] == '\t') {
            ++pos;
        }
        
        if (str[pos] == '\0') {
            if (endPos) *endPos = pos;
            return false;
        }
        
        bool negative = false;
        if constexpr (std::is_signed_v<T>) {
            if (str[pos] == '-') {
                negative = true;
                ++pos;
            } else if (str[pos] == '+') {
                ++pos;
            }
        } else {
            if (str[pos] == '-') {
                if (endPos) *endPos = pos;
                return false; // Unsigned type can't be negative
            } else if (str[pos] == '+') {
                ++pos;
            }
        }
        
        // Must have at least one digit
        if (str[pos] < '0' || str[pos] > '9') {
            if (endPos) *endPos = pos;
            return false;
        }
        
        using UnsignedT = std::make_unsigned_t<T>;
        UnsignedT uResult = 0;
        UnsignedT maxValue = negative ? static_cast<UnsignedT>(-(std::numeric_limits<T>::min() + 1)) + 1
                                      : static_cast<UnsignedT>(std::numeric_limits<T>::max());
        
        while (str[pos] >= '0' && str[pos] <= '9') {
            UnsignedT digit = str[pos] - '0';
            
            // Check for overflow
            if (uResult > maxValue / 10 || (uResult == maxValue / 10 && digit > maxValue % 10)) {
                if (endPos) *endPos = pos;
                return false;
            }
            
            uResult = uResult * 10 + digit;
            ++pos;
        }
        
        if (negative) {
            result = -static_cast<T>(uResult);
        } else {
            result = static_cast<T>(uResult);
        }
        
        if (endPos) *endPos = pos;
        return true;
    }
    
    
    // Static string to float/double conversion
    template <typename T>
    static std::enable_if_t<std::is_floating_point_v<T>, bool>
    StringToFloat(const char* str, T& result, size_t* endPos = nullptr) {
        if (!str) return false;
        
        size_t pos = 0;
        result = 0;
        
        // Skip whitespace
        while (str[pos] == ' ' || str[pos] == '\t') {
            ++pos;
        }
        
        if (str[pos] == '\0') {
            if (endPos) *endPos = pos;
            return false;
        }
        
        bool negative = false;
        if (str[pos] == '-') {
            negative = true;
            ++pos;
        } else if (str[pos] == '+') {
            ++pos;
        }
        
        // Check for special values
        if (str[pos] == 'I' || str[pos] == 'i') {
            if ((str[pos+1] == 'n' || str[pos+1] == 'N') && 
                (str[pos+2] == 'f' || str[pos+2] == 'F')) {
                result = negative ? -std::numeric_limits<T>::infinity() 
                                  : std::numeric_limits<T>::infinity();
                pos += 3;
                if (endPos) *endPos = pos;
                return true;
            }
        }
        
        if (str[pos] == 'N' || str[pos] == 'n') {
            if ((str[pos+1] == 'a' || str[pos+1] == 'A') && 
                (str[pos+2] == 'n' || str[pos+2] == 'N')) {
                result = std::numeric_limits<T>::quiet_NaN();
                pos += 3;
                if (endPos) *endPos = pos;
                return true;
            }
        }
        
        // Parse number
        T integerPart = 0;
        T fractionalPart = 0;
        T divisor = 1;
        bool hasInteger = false;
        bool hasFraction = false;
        
        // Integer part
        while (str[pos] >= '0' && str[pos] <= '9') {
            integerPart = integerPart * 10 + (str[pos] - '0');
            hasInteger = true;
            ++pos;
        }
        
        // Fractional part
        if (str[pos] == '.') {
            ++pos;
            while (str[pos] >= '0' && str[pos] <= '9') {
                fractionalPart = fractionalPart * 10 + (str[pos] - '0');
                divisor *= 10;
                hasFraction = true;
                ++pos;
            }
        }
        
        if (!hasInteger && !hasFraction) {
            if (endPos) *endPos = pos;
            return false;
        }
        
        result = integerPart + (fractionalPart / divisor);
        
        // Handle exponent
        if (str[pos] == 'e' || str[pos] == 'E') {
            ++pos;
            bool expNegative = false;
            if (str[pos] == '-') {
                expNegative = true;
                ++pos;
            } else if (str[pos] == '+') {
                ++pos;
            }
            
            int exponent = 0;
            bool hasExponent = false;
            while (str[pos] >= '0' && str[pos] <= '9') {
                exponent = exponent * 10 + (str[pos] - '0');
                hasExponent = true;
                ++pos;
            }
            
            if (hasExponent) {
                T multiplier = 1;
                for (int i = 0; i < exponent; ++i) {
                    multiplier *= 10;
                }
                
                if (expNegative) {
                    result /= multiplier;
                } else {
                    result *= multiplier;
                }
            }
        }
        
        if (negative) {
            result = -result;
        }
        
        if (endPos) *endPos = pos;
        return true;
    }


private:
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
