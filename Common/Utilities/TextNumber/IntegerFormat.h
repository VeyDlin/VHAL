#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>


enum class NumberBase : uint8_t {
	Binary = 2,
	Octal = 8,
	Decimal = 10,
	Hexadecimal = 16
};


// Writes value in the given base into output and returns the number of characters. Returns 0 and writes nothing when the text
// does not fit. The rules are those of Print::NumberToString: decimal shows a minus sign for a negative value; the other bases
// show the two's complement at the width of the type (int8 -1 is FF in hexadecimal); binary always shows every bit of the type.
template <typename T>
constexpr size_t FormatInteger(std::span<char> output, T value, NumberBase base = NumberBase::Decimal) {
	static_assert(std::is_integral_v<T> && sizeof(T) <= sizeof(uint64_t), "FormatInteger needs an integer of at most 64 bits");

	bool isNegative = false;
	uint64_t magnitude = static_cast<uint64_t>(value);
	if constexpr (std::is_signed_v<T>) {
		if (value < 0 && base == NumberBase::Decimal) {
			isNegative = true;
			magnitude = static_cast<uint64_t>(0) - static_cast<uint64_t>(value);
		}
	}

	if constexpr (sizeof(T) < sizeof(uint64_t)) {
		if (base != NumberBase::Decimal) {
			magnitude &= (static_cast<uint64_t>(1) << (sizeof(T) * 8)) - 1;
		}
	}

	constexpr char digits[] = "0123456789ABCDEF";
	char reversed[66] = {};
	size_t reversedCount = 0;
	char text[66] = {};
	size_t length = 0;

	if (base == NumberBase::Binary) {
		size_t bits = sizeof(T) * 8;
		for (size_t index = 0; index < bits; index++) {
			text[length++] = ((magnitude >> (bits - 1 - index)) & 1u) != 0 ? '1' : '0';
		}
	} else {
		uint64_t radix = static_cast<uint64_t>(base);
		do {
			reversed[reversedCount++] = digits[magnitude % radix];
			magnitude /= radix;
		} while (magnitude > 0);

		if (isNegative) {
			reversed[reversedCount++] = '-';
		}

		for (size_t index = 0; index < reversedCount; index++) {
			text[length++] = reversed[reversedCount - 1 - index];
		}
	}

	if (length > output.size()) {
		return 0;
	}

	for (size_t index = 0; index < length; index++) {
		output[index] = text[index];
	}

	return length;
}
