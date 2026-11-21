#pragma once
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <type_traits>


namespace NumberDetail {


// Unsigned integer of 192 bits, enough for a 53-bit mantissa times 10^40
struct WideUint {
	static constexpr size_t limbCount = 6;
	uint32_t limbs[limbCount] = {};


	constexpr void MultiplyBy10() {
		uint64_t carry = 0;
		for (size_t index = 0; index < limbCount; index++) {
			uint64_t product = static_cast<uint64_t>(limbs[index]) * 10 + carry;
			limbs[index] = static_cast<uint32_t>(product);
			carry = product >> 32;
		}
	}


	constexpr bool Bit(uint32_t index) const {
		size_t limb = index / 32;
		return limb < limbCount && ((limbs[limb] >> (index % 32)) & 1u) != 0;
	}


	constexpr void ShiftRight(uint32_t bits) {
		size_t limbShift = bits / 32;
		uint32_t bitShift = bits % 32;
		uint32_t shifted[limbCount] = {};
		for (size_t index = 0; index + limbShift < limbCount; index++) {
			uint64_t low = limbs[index + limbShift];
			uint64_t high = index + limbShift + 1 < limbCount ? limbs[index + limbShift + 1] : 0;
			shifted[index] = static_cast<uint32_t>(((high << 32) | low) >> bitShift);
		}

		for (size_t index = 0; index < limbCount; index++) {
			limbs[index] = shifted[index];
		}
	}


	constexpr void Increment() {
		for (size_t index = 0; index < limbCount; index++) {
			if (++limbs[index] != 0) {
				break;
			}
		}
	}


	// Divides by 10 in place and returns the remainder
	constexpr uint32_t DivideBy10() {
		uint64_t remainder = 0;
		for (size_t index = limbCount; index > 0; index--) {
			uint64_t current = (remainder << 32) | limbs[index - 1];
			limbs[index - 1] = static_cast<uint32_t>(current / 10);
			remainder = current % 10;
		}

		return static_cast<uint32_t>(remainder);
	}


	constexpr bool IsZero() const {
		for (size_t index = 0; index < limbCount; index++) {
			if (limbs[index] != 0) {
				return false;
			}
		}

		return true;
	}
};


// Float output keeps at most this many digits after the decimal point
constexpr uint8_t kMaxFractionDigits = 40;

// A finite double is below 2^1024, which has 309 decimal digits
constexpr size_t kIntegerLimbs = 32;
constexpr size_t kMaxDecimalDigits = 320;


constexpr bool LimbsAreZero(const uint32_t* limbs, size_t count) {
	for (size_t index = 0; index < count; index++) {
		if (limbs[index] != 0) {
			return false;
		}
	}

	return true;
}


// Writes value in decimal and returns the number of characters written (no terminator); capacity must hold them
constexpr size_t WriteDecimal(char* out, size_t capacity, uint64_t value) {
	char reversed[20] = {};
	size_t count = 0;
	do {
		reversed[count++] = static_cast<char>('0' + value % 10);
		value /= 10;
	} while (value > 0);

	size_t length = 0;
	while (count > 0 && length < capacity) {
		out[length++] = reversed[--count];
	}

	return length;
}


// Rounds value (0 <= value < 2^64) to digitCount decimal places, half away from zero.
// Works on the exact binary value: repeated multiplication by 10 would round at every step.
// Stores the fraction digits in fractionDigits and returns the integer part, which
// becomes one larger when rounding carries into it.
constexpr uint64_t RoundFixed(double value, uint8_t digitCount, uint8_t* fractionDigits) {
	uint64_t integerPart = static_cast<uint64_t>(value);
	double fraction = value - static_cast<double>(integerPart);

	// Doubling is exact, so afterwards fraction == mantissa / 2^fractionBits exactly.
	// The check is for a whole number; the value stays below 2^54 here, so the conversion is safe.
	uint32_t fractionBits = 0;
	while (fraction != static_cast<double>(static_cast<uint64_t>(fraction))) {
		fraction *= 2;
		++fractionBits;
	}

	uint64_t mantissa = static_cast<uint64_t>(fraction);

	// numerator = mantissa * 10^digitCount; the scaled fraction is numerator / 2^fractionBits
	WideUint numerator;
	numerator.limbs[0] = static_cast<uint32_t>(mantissa);
	numerator.limbs[1] = static_cast<uint32_t>(mantissa >> 32);
	for (uint8_t index = 0; index < digitCount; ++index) {
		numerator.MultiplyBy10();
	}

	// The bit just below the scaled binary point is set exactly when the remainder is at least one half
	bool roundUp = fractionBits > 0 && numerator.Bit(fractionBits - 1);
	numerator.ShiftRight(fractionBits);
	if (roundUp) {
		numerator.Increment();
	}

	for (uint8_t index = digitCount; index > 0; --index) {
		fractionDigits[index - 1] = static_cast<uint8_t>(numerator.DivideBy10());
	}

	// Only a carry into the integer part leaves a non-zero value here
	if (!numerator.IsZero()) {
		integerPart++;
	}

	return integerPart;
}


constexpr size_t FixedDigits(char* out, size_t capacity, double value, uint8_t precision) {
	uint8_t digitCount = precision < kMaxFractionDigits ? precision : kMaxFractionDigits;
	uint8_t fractionDigits[kMaxFractionDigits] = {};
	uint64_t integerPart = RoundFixed(value, digitCount, fractionDigits);

	size_t length = WriteDecimal(out, capacity, integerPart);
	if (digitCount > 0 && length + 1 + digitCount <= capacity) {
		out[length++] = '.';
		for (uint8_t index = 0; index < digitCount; ++index) {
			out[length++] = static_cast<char>('0' + fractionDigits[index]);
		}
	}

	return length;
}


// Writes d.ddde+N for finite values of at least 2^64. The value is an integer, so its exact
// decimal digits are known; the digit after the requested precision decides the rounding,
// which is half away from zero on the exact value.
constexpr size_t ScientificDigits(char* out, size_t capacity, double value, uint8_t precision) {
	uint8_t digitCount = precision < kMaxFractionDigits ? precision : kMaxFractionDigits;

	// value == mantissa * 2^shift exactly, with a 53-bit mantissa for a normal value of at least 2^64
	uint64_t bits = std::bit_cast<uint64_t>(value);
	int64_t biasedExponent = static_cast<int64_t>((bits >> 52) & 0x7FF);
	uint64_t mantissa = (bits & ((static_cast<uint64_t>(1) << 52) - 1)) | (static_cast<uint64_t>(1) << 52);
	int64_t shift = biasedExponent - 1075;

	uint32_t limbs[kIntegerLimbs] = {};
	for (int64_t bit = 0; bit < 53; ++bit) {
		if (((mantissa >> bit) & 1u) != 0) {
			size_t target = static_cast<size_t>(shift + bit);
			limbs[target / 32] |= 1u << (target % 32);
		}
	}

	// Decimal digits of the integer, least significant first
	char reversed[kMaxDecimalDigits] = {};
	size_t total = 0;
	do {
		uint64_t remainder = 0;
		for (size_t index = kIntegerLimbs; index > 0; index--) {
			uint64_t current = (remainder << 32) | limbs[index - 1];
			limbs[index - 1] = static_cast<uint32_t>(current / 10);
			remainder = current % 10;
		}

		reversed[total++] = static_cast<char>('0' + remainder);
	} while (!LimbsAreZero(limbs, kIntegerLimbs));

	// digits[0] is the leading digit; positions past the integer's length are 0
	uint8_t digits[kMaxFractionDigits + 2] = {};
	for (size_t index = 0; index <= digitCount + 1u; ++index) {
		digits[index] = index < total ? static_cast<uint8_t>(reversed[total - 1 - index] - '0') : 0;
	}

	uint64_t exponent = total - 1;
	if (digits[digitCount + 1] >= 5) {
		bool carry = true;
		for (size_t index = digitCount + 1; index > 0 && carry; --index) {
			if (digits[index - 1] == 9) {
				digits[index - 1] = 0;
			} else {
				digits[index - 1]++;
				carry = false;
			}
		}

		if (carry) {
			// 9.99... rounded up: the mantissa becomes 1.00... with one more exponent step
			digits[0] = 1;
			exponent++;
		}
	}

	size_t length = 0;
	out[length++] = static_cast<char>('0' + digits[0]);
	if (digitCount > 0) {
		out[length++] = '.';
		for (uint8_t index = 1; index <= digitCount; ++index) {
			out[length++] = static_cast<char>('0' + digits[index]);
		}
	}

	out[length++] = 'e';
	out[length++] = '+';
	length += WriteDecimal(&out[length], capacity - length, exponent);
	return length;
}


// The text of a double, as Print::FloatToString writes it before the copy into the caller's buffer
constexpr size_t FloatText(char* buffer, size_t bufferSize, double number, uint8_t precision) {
	if (bufferSize == 0) {
		return 0;
	}

	char tempBuffer[64] = {};
	size_t index = 0;

	bool isNegative = false;
	if (number < 0) {
		isNegative = true;
		number = -number;
	}

	// NaN is the only value that is not equal to itself
	if (number != number) {
		const char* text = "NaN";
		size_t length = 3;
		if (length < bufferSize) {
			for (size_t offset = 0; offset < length; ++offset) {
				buffer[offset] = text[offset];
			}
			buffer[length] = '\0';
			return length;
		}

		return 0;
	}

	if (number > std::numeric_limits<double>::max()) {
		const char* text = isNegative ? "-Inf" : "Inf";
		size_t length = isNegative ? 4 : 3;
		if (length < bufferSize) {
			for (size_t offset = 0; offset < length; ++offset) {
				buffer[offset] = text[offset];
			}
			buffer[length] = '\0';
			return length;
		}

		return 0;
	}

	if (isNegative) {
		tempBuffer[index++] = '-';
	}

	// A uint64 holds every integer part below 2^64; larger values use scientific notation
	if (number >= 18446744073709551616.0) {
		index += ScientificDigits(&tempBuffer[index], sizeof(tempBuffer) - index, number, precision);
	} else {
		index += FixedDigits(&tempBuffer[index], sizeof(tempBuffer) - index, number, precision);
	}

	size_t copyLength = index < bufferSize - 1 ? index : bufferSize - 1;
	for (size_t offset = 0; offset < copyLength; ++offset) {
		buffer[offset] = tempBuffer[offset];
	}

	buffer[copyLength] = '\0';
	return copyLength;
}


}


// Writes a float or double with the rules of Print::FloatToString: fixed point with precision digits after the point (at most
// 40, rounded half away from zero on the exact binary value), scientific form from 2^64 up, and NaN, Inf and -Inf as text.
// Returns the number of characters, or 0 and writes nothing when the text does not fit (Print truncates instead, which is a
// caller concern). The conversion is constexpr: it uses bit_cast instead of frexp and ldexp.
template <typename T>
constexpr size_t FormatFloat(std::span<char> output, T value, uint8_t precision = 2) {
	static_assert(std::is_floating_point_v<T>, "FormatFloat needs a floating-point type");

	char text[64] = {};
	size_t length = NumberDetail::FloatText(text, sizeof(text), static_cast<double>(value), precision);
	if (length == 0 || length > output.size()) {
		return 0;
	}

	for (size_t index = 0; index < length; index++) {
		output[index] = text[index];
	}

	return length;
}
