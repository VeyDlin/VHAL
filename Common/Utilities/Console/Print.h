#pragma once
#include <VHAL.h>
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


    template <typename T>
    static std::enable_if_t<std::is_integral_v<T>, size_t>
    NumberToString(char* buffer, size_t bufferSize, T number, Format format = Format::Dec) {
        bool isNegative = false;
        uint64 unsignedNumber;

        if constexpr (std::is_signed_v<T>) {
            if (number < 0 && format == Format::Dec) {
                isNegative = true;
				unsignedNumber = static_cast<uint64>(-(static_cast<int64_t>(number) + 1)) + 1;
            } else {
                unsignedNumber = static_cast<uint64>(number);
            }
        } else {
            unsignedNumber = static_cast<uint64>(number);
        }

		// Hex and octal of a negative value show the type's width (int8 -1 -> FF), like Bin does
		if (format != Format::Dec && sizeof(T) < sizeof(uint64)) {
			unsignedNumber &= (uint64(1) << (sizeof(T) * 8)) - 1;
		}

        return NumberToStringCore(buffer, bufferSize, unsignedNumber, isNegative, sizeof(T), format);
    }


    template <typename T>
    static std::enable_if_t<std::is_floating_point_v<T>, size_t>
    FloatToString(char* buffer, size_t bufferSize, T number, uint8_t precision = 2) {
        return FloatToStringCore(buffer, bufferSize, static_cast<double>(number), precision);
    }


    template <typename T>
    static std::enable_if_t<std::is_integral_v<T>, bool>
    StringToNumber(const char* str, T& result, size_t* endPos = nullptr) {
        int64_t temp;
        bool success = StringToNumberCore(str, temp, std::is_signed_v<T>, sizeof(T), endPos);
        if (success) {
            result = static_cast<T>(temp);
        }
        return success;
    }


    template <typename T>
    static std::enable_if_t<std::is_floating_point_v<T>, bool>
    StringToFloat(const char* str, T& result, size_t* endPos = nullptr) {
        double temp;
        bool success = StringToFloatCore(str, temp, endPos);
        if (success) {
            result = static_cast<T>(temp);
        }
        return success;
    }


private:
    static size_t NumberToStringCore(char* buffer, size_t bufferSize, uint64_t number, bool isNegative, uint8_t typeSize, Format format) {
        if (bufferSize == 0) return 0;
        
        size_t idx = 0;
        char tempBuffer[66]; // Max for binary representation of 64-bit number + null
        
        // Handle zero case for decimal
        if (number == 0 && format == Format::Dec) {
            if (bufferSize > 1) {
                buffer[0] = '0';
                buffer[1] = '\0';
                return 1;
            }
            return 0;
        }

        if (format == Format::Bin) {
            size_t bits = typeSize * 8;
            for (size_t i = 0; i < bits && idx < sizeof(tempBuffer) - 1; ++i) {
                tempBuffer[idx++] = (number & (1ULL << (bits - 1 - i))) ? '1' : '0';
            }
        } else if (format == Format::Hex) {
            static const char* hex_digits = "0123456789ABCDEF";
            do {
                tempBuffer[idx++] = hex_digits[number % 16];
                number /= 16;
            } while (number > 0 && idx < sizeof(tempBuffer) - 1);
            // Reverse hex digits
            for (size_t i = 0; i < idx / 2; ++i) {
                char temp = tempBuffer[i];
                tempBuffer[i] = tempBuffer[idx - i - 1];
                tempBuffer[idx - i - 1] = temp;
            }
        } else if (format == Format::Oct) {
            do {
                tempBuffer[idx++] = '0' + (number % 8);
                number /= 8;
            } while (number > 0 && idx < sizeof(tempBuffer) - 1);
            // Reverse oct digits
            for (size_t i = 0; i < idx / 2; ++i) {
                char temp = tempBuffer[i];
                tempBuffer[i] = tempBuffer[idx - i - 1];
                tempBuffer[idx - i - 1] = temp;
            }
        } else { // Format::Dec
            size_t num_start = idx;
            do {
                tempBuffer[idx++] = '0' + (number % 10);
                number /= 10;
            } while (number > 0 && idx < sizeof(tempBuffer) - 1);
            
            // Reverse digits
            for (size_t i = num_start; i < num_start + (idx - num_start) / 2; ++i) {
                char temp = tempBuffer[i];
                tempBuffer[i] = tempBuffer[idx - (i - num_start) - 1];
                tempBuffer[idx - (i - num_start) - 1] = temp;
            }
            
            if (isNegative && idx < sizeof(tempBuffer) - 1) {
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
        for (size_t i = 0; i < copyLen; ++i) {
            buffer[i] = tempBuffer[i];
        }
        buffer[copyLen] = '\0';
        return copyLen;
    }
    
    
    static size_t FloatToStringCore(char* buffer, size_t bufferSize, double number, uint8_t precision) {
        if (bufferSize == 0) return 0;
        
        char tempBuffer[64];
        size_t idx = 0;

        bool isNegative = false;
        if (number < 0) {
            isNegative = true;
            number = -number;
        }

        // Handle special cases using direct bit comparison instead of std functions
        if (number != number) { // NaN check without std::isnan
            const char* str = isNegative ? "-NaN" : "NaN";
            size_t len = isNegative ? 4 : 3;
            if (len < bufferSize) {
                for (size_t i = 0; i < len; ++i) {
                    buffer[i] = str[i];
                }
                buffer[len] = '\0';
                return len;
            }
            return 0;
        }
        
		if (number > std::numeric_limits<double>::max()) { // Only infinity is above DBL_MAX
            const char* str = isNegative ? "-Inf" : "Inf";
            size_t len = isNegative ? 4 : 3;
            if (len < bufferSize) {
                for (size_t i = 0; i < len; ++i) {
                    buffer[i] = str[i];
                }
                buffer[len] = '\0';
                return len;
            }
            return 0;
        }

        if (isNegative) {
            tempBuffer[idx++] = '-';
        }

		// A uint64 holds every integer part below 2^64; larger values use scientific notation
		if (number >= 18446744073709551616.0) {
			idx += ScientificDigits(&tempBuffer[idx], sizeof(tempBuffer) - idx, number, precision);
		} else {
			idx += FixedDigits(&tempBuffer[idx], sizeof(tempBuffer) - idx, number, precision);
		}

        // Copy to output buffer
        size_t copyLen = (idx < bufferSize - 1) ? idx : bufferSize - 1;
        for (size_t i = 0; i < copyLen; ++i) {
            buffer[i] = tempBuffer[i];
        }
        buffer[copyLen] = '\0';
        return copyLen;
    }
    
    
	// Float output keeps at most this many digits after the decimal point
	static constexpr uint8_t kMaxFractionDigits = 40;


	// Writes value in decimal, returns the number of characters written (no terminator)
	static size_t WriteDecimal(char* out, size_t capacity, uint64_t value) {
		char reversed[20];
		size_t count = 0;
		do {
			reversed[count++] = static_cast<char>('0' + value % 10);
			value /= 10;
		} while (value > 0);

		size_t len = 0;
		while (count > 0 && len < capacity) {
			out[len++] = reversed[--count];
		}
		return len;
	}


	// Rounds value (0 <= value < 2^64) to digitCount decimal places, half away from zero.
	// Stores the fraction digits in fractionDigits and returns the integer part, which
	// becomes one larger when rounding carries into it.
	static uint64_t RoundFixed(double value, uint8_t digitCount, uint8_t* fractionDigits) {
		uint64_t integerPart = static_cast<uint64_t>(value);
		double fraction = value - static_cast<double>(integerPart);

		for (uint8_t i = 0; i < digitCount; ++i) {
			fraction *= 10;
			uint8_t digit = static_cast<uint8_t>(fraction);
			fractionDigits[i] = digit;
			fraction -= digit;
		}

		if (fraction >= 0.5) {
			bool carry = true;
			for (uint8_t i = digitCount; i > 0 && carry; --i) {
				fractionDigits[i - 1]++;
				if (fractionDigits[i - 1] == 10) {
					fractionDigits[i - 1] = 0;
				} else {
					carry = false;
				}
			}
			if (carry) {
				integerPart++;
			}
		}
		return integerPart;
	}


	static size_t FixedDigits(char* out, size_t capacity, double value, uint8_t precision) {
		uint8_t digitCount = precision < kMaxFractionDigits ? precision : kMaxFractionDigits;
		uint8_t fractionDigits[kMaxFractionDigits] = {};
		uint64_t integerPart = RoundFixed(value, digitCount, fractionDigits);

		size_t len = WriteDecimal(out, capacity, integerPart);
		if (digitCount > 0 && len + 1 + digitCount <= capacity) {
			out[len++] = '.';
			for (uint8_t i = 0; i < digitCount; ++i) {
				out[len++] = static_cast<char>('0' + fractionDigits[i]);
			}
		}
		return len;
	}


	// Writes d.ddde+N for finite values of at least 2^64; the mantissa has the requested precision
	static size_t ScientificDigits(char* out, size_t capacity, double value, uint8_t precision) {
		uint8_t digitCount = precision < kMaxFractionDigits ? precision : kMaxFractionDigits;
		uint8_t fractionDigits[kMaxFractionDigits] = {};

		uint64_t exponent = 0;
		double mantissa = value;
		while (mantissa >= 10.0) {
			mantissa /= 10.0;
			exponent++;
		}

		uint64_t leadDigit = RoundFixed(mantissa, digitCount, fractionDigits);
		if (leadDigit == 10) {
			// Rounding carried out of the mantissa; all fraction digits are already 0
			leadDigit = 1;
			exponent++;
		}

		size_t len = 0;
		out[len++] = static_cast<char>('0' + leadDigit);
		if (digitCount > 0) {
			out[len++] = '.';
			for (uint8_t i = 0; i < digitCount; ++i) {
				out[len++] = static_cast<char>('0' + fractionDigits[i]);
			}
		}
		out[len++] = 'e';
		out[len++] = '+';
		len += WriteDecimal(&out[len], capacity - len, exponent);
		return len;
	}


    static bool StringToNumberCore(const char* str, int64_t& result, bool isSigned, uint8_t typeSize, size_t* endPos = nullptr) {
        if (!str) return false;
        
        size_t pos = 0;
        
        // Skip whitespace
        while (str[pos] == ' ' || str[pos] == '\t') {
            ++pos;
        }
        
        if (str[pos] == '\0') {
            if (endPos) *endPos = pos;
            return false;
        }
        
        bool negative = false;
        if (isSigned && str[pos] == '-') {
            negative = true;
            ++pos;
        } else if (str[pos] == '+') {
            ++pos;
        } else if (!isSigned && str[pos] == '-') {
            if (endPos) *endPos = pos;
            return false; // Unsigned type can't be negative
        }
        
        // Must have at least one digit
        if (str[pos] < '0' || str[pos] > '9') {
            if (endPos) *endPos = pos;
            return false;
        }
        
        uint64_t uResult = 0;
        
        // Calculate max value based on type size and sign
        uint64_t maxValue;
        if (isSigned) {
            if (negative) {
                // For signed negative: use absolute value of min value
                switch (typeSize) {
                    case 1: maxValue = 128; break;           // -128
                    case 2: maxValue = 32768; break;         // -32768
                    case 4: maxValue = 2147483648ULL; break; // -2147483648
                    case 8: maxValue = 9223372036854775808ULL; break; // -9223372036854775808
                    default: maxValue = 9223372036854775808ULL; break;
                }
            } else {
                // For signed positive: use max positive value
                switch (typeSize) {
                    case 1: maxValue = 127; break;           // 127
                    case 2: maxValue = 32767; break;         // 32767
                    case 4: maxValue = 2147483647; break;    // 2147483647
                    case 8: maxValue = 9223372036854775807ULL; break; // 9223372036854775807
                    default: maxValue = 9223372036854775807ULL; break;
                }
            }
        } else {
            // For unsigned: use max unsigned value
            switch (typeSize) {
                case 1: maxValue = 255; break;               // 255
                case 2: maxValue = 65535; break;             // 65535
                case 4: maxValue = 4294967295ULL; break;     // 4294967295
                case 8: maxValue = 18446744073709551615ULL; break; // 18446744073709551615
                default: maxValue = 18446744073709551615ULL; break;
            }
        }
        
        while (str[pos] >= '0' && str[pos] <= '9') {
            uint64_t digit = str[pos] - '0';
            
            // Check for overflow
            if (uResult > maxValue / 10 || (uResult == maxValue / 10 && digit > maxValue % 10)) {
                if (endPos) *endPos = pos;
                return false;
            }
            
            uResult = uResult * 10 + digit;
            ++pos;
        }
        
        if (negative) {
			result = static_cast<int64_t>(0 - uResult);
        } else {
            result = static_cast<int64_t>(uResult);
        }
        
        if (endPos) *endPos = pos;
        return true;
    }

    
    static bool StringToFloatCore(const char* str, double& result, size_t* endPos = nullptr) {
        if (!str) return false;
        
        size_t pos = 0;
        
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
        
        // Check for special values without std::numeric_limits
        if (str[pos] == 'I' || str[pos] == 'i') {
            if ((str[pos+1] == 'n' || str[pos+1] == 'N') && 
                (str[pos+2] == 'f' || str[pos+2] == 'F')) {
                result = negative ? -1.0/0.0 : 1.0/0.0; // Infinity without std
                pos += 3;
                if (endPos) *endPos = pos;
                return true;
            }
        }
        
        if (str[pos] == 'N' || str[pos] == 'n') {
            if ((str[pos+1] == 'a' || str[pos+1] == 'A') && 
                (str[pos+2] == 'n' || str[pos+2] == 'N')) {
                result = 0.0/0.0; // NaN without std
                pos += 3;
                if (endPos) *endPos = pos;
                return true;
            }
        }
        
        // Parse number
        double integerPart = 0;
        double fractionalPart = 0;
        double divisor = 1;
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
                double multiplier = 1;
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
