#pragma once
#include "NumberError.h"
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>
#include <type_traits>


// Rules for reading an integer. Each setter returns a changed copy, so a whole set of options is one constant expression:
// IntegerOptions<uint8>{}.WithRange(0, 100).WithHexPrefix().
template <typename T>
struct IntegerOptions {
	bool allowHexPrefix = false;
	bool allowBinaryPrefix = false;
	bool allowPlusSign = false;
	bool allowMinusSign = true;
	bool skipLeadingBlanks = false;
	T minimum = std::numeric_limits<T>::min();
	T maximum = std::numeric_limits<T>::max();


	constexpr IntegerOptions WithHexPrefix() const {
		IntegerOptions copy = *this;
		copy.allowHexPrefix = true;
		return copy;
	}


	constexpr IntegerOptions WithBinaryPrefix() const {
		IntegerOptions copy = *this;
		copy.allowBinaryPrefix = true;
		return copy;
	}


	constexpr IntegerOptions WithPlusSign() const {
		IntegerOptions copy = *this;
		copy.allowPlusSign = true;
		return copy;
	}


	// Has no effect on an unsigned type, which never takes a minus sign.
	constexpr IntegerOptions WithoutMinusSign() const {
		IntegerOptions copy = *this;
		copy.allowMinusSign = false;
		return copy;
	}


	// Only the whole-string helpers read this: a streaming parser has no leading blanks to skip.
	constexpr IntegerOptions WithLeadingBlanks() const {
		IntegerOptions copy = *this;
		copy.skipLeadingBlanks = true;
		return copy;
	}


	constexpr IntegerOptions WithRange(T low, T high) const {
		IntegerOptions copy = *this;
		copy.minimum = low;
		copy.maximum = high;
		return copy;
	}
};


// Reads an integer one character at a time. Push accepts or rejects a character and changes nothing when it rejects it;
// Pop undoes the last accepted character exactly; Finish gives the value or the error.
//
// The magnitude is 64 bits. A digit that would overflow it is counted in extraDigits and not stored, so Pop removes those
// first. Any value with extra digits is at least 2^64, out of range for every type this handles.
template <typename T>
class IntegerParser {
	static_assert(std::is_integral_v<T> && sizeof(T) <= sizeof(uint64_t), "IntegerParser needs an integer of at most 64 bits");


private:
	IntegerOptions<T> options;
	uint64_t magnitude = 0;
	uint64_t extraDigits = 0;
	size_t digitCount = 0;
	size_t acceptedCount = 0;
	uint8_t prefixBase = 0;
	bool negative = false;


public:
	constexpr explicit IntegerParser(IntegerOptions<T> parserOptions = {}) : options(parserOptions) {
	}


	// The first character may be a sign. Any other character must be a digit of the base in use.
	constexpr NumberError<T> Push(char character) {
		if (acceptedCount == 0 && (character == '+' || character == '-')) {
			return PushSign(character);
		}

		// A prefix letter can only follow a lone leading zero, and only when that prefix is allowed.
		if (prefixBase == 0 && digitCount == 1 && magnitude == 0 && extraDigits == 0) {
			if ((character == 'x' || character == 'X') && options.allowHexPrefix) {
				prefixBase = 16;
				digitCount = 0;
				acceptedCount++;
				return NumberError<T>{};
			}

			if ((character == 'b' || character == 'B') && options.allowBinaryPrefix) {
				prefixBase = 2;
				digitCount = 0;
				acceptedCount++;
				return NumberError<T>{};
			}
		}

		uint8_t base = CurrentBase();
		int digit = DigitValue(character, base);
		if (digit < 0) {
			return MakeError(NumberErrorCode::InvalidCharacter);
		}

		AppendDigit(static_cast<uint64_t>(digit), base);
		return NumberError<T>{};
	}


	// Undoes the last accepted character. Does nothing on an empty parser.
	constexpr void Pop() {
		if (acceptedCount == 0) {
			return;
		}

		if (digitCount > 0) {
			digitCount--;
			if (extraDigits > 0) {
				extraDigits--;
			} else {
				magnitude /= CurrentBase();
			}
			acceptedCount--;
			return;
		}

		if (prefixBase != 0) {
			// Back to the lone leading zero, which is still a digit
			prefixBase = 0;
			digitCount = 1;
			acceptedCount--;
			return;
		}

		negative = false;
		acceptedCount--;
	}


	constexpr NumberResult<T> Finish() const {
		NumberResult<T> result;
		result.endPosition = acceptedCount;

		if (prefixBase != 0 && digitCount == 0) {
			result.error = MakeError(NumberErrorCode::PrefixWithoutDigits);
			return result;
		}

		if (digitCount == 0) {
			result.error = MakeError(NumberErrorCode::Empty);
			return result;
		}

		if (!FitsRange(negative, magnitude)) {
			result.error = MakeError(NumberErrorCode::OutOfRange);
			return result;
		}

		result.value = negative ? static_cast<T>(static_cast<uint64_t>(0) - magnitude) : static_cast<T>(magnitude);
		return result;
	}


private:
	constexpr NumberError<T> PushSign(char character) {
		if (character == '-' && (!std::is_signed_v<T> || !options.allowMinusSign)) {
			return MakeError(NumberErrorCode::SignNotAllowed);
		}

		if (character == '+' && !options.allowPlusSign) {
			return MakeError(NumberErrorCode::SignNotAllowed);
		}

		negative = character == '-';
		acceptedCount++;
		return NumberError<T>{};
	}


	constexpr void AppendDigit(uint64_t digit, uint8_t base) {
		if (extraDigits > 0 || magnitude > (std::numeric_limits<uint64_t>::max() - digit) / base) {
			extraDigits++;
		} else {
			magnitude = magnitude * base + digit;
		}

		digitCount++;
		acceptedCount++;
	}


	constexpr uint8_t CurrentBase() const {
		return prefixBase != 0 ? prefixBase : 10;
	}


	static constexpr int DigitValue(char character, uint8_t base) {
		if (character >= '0' && character <= '9') {
			int digit = character - '0';
			return digit < base ? digit : -1;
		}

		if (base == 16 && character >= 'a' && character <= 'f') {
			return character - 'a' + 10;
		}

		if (base == 16 && character >= 'A' && character <= 'F') {
			return character - 'A' + 10;
		}

		return -1;
	}


	// The magnitude of a value, which also works for the minimum of a signed type (its magnitude does not fit the type).
	static constexpr uint64_t MagnitudeOf(T value) {
		if constexpr (std::is_signed_v<T>) {
			if (value < T{}) {
				return static_cast<uint64_t>(0) - static_cast<uint64_t>(value);
			}
		}

		return static_cast<uint64_t>(value);
	}


	static constexpr bool IsNegativeValue(T value) {
		if constexpr (std::is_signed_v<T>) {
			return value < T{};
		}

		return false;
	}


	constexpr bool FitsRange(bool isNegative, uint64_t absolute) const {
		if (extraDigits > 0) {
			return false;
		}

		if (isNegative && absolute > 0) {
			// -absolute >= minimum needs minimum <= 0 and absolute <= |minimum|
			if (options.minimum > T{}) {
				return false;
			}

			if (absolute > MagnitudeOf(options.minimum)) {
				return false;
			}

			// -absolute <= maximum holds at once when maximum >= 0
			return !IsNegativeValue(options.maximum) || absolute >= MagnitudeOf(options.maximum);
		}

		if (IsNegativeValue(options.maximum) || absolute > MagnitudeOf(options.maximum)) {
			return false;
		}

		return options.minimum <= T{} || absolute >= MagnitudeOf(options.minimum);
	}


	constexpr NumberError<T> MakeError(NumberErrorCode code) const {
		NumberError<T> error;
		error.code = code;
		error.position = acceptedCount;
		error.minimum = options.minimum;
		error.maximum = options.maximum;
		return error;
	}


};


// Reads the longest prefix of text that the grammar accepts, as Print::StringToNumber does: "12abc" gives 12 and an end
// position of 2. The error is the one for the whole input, except that a text with no digits reports the character that
// stopped it, so "-5" for an unsigned type reports SignNotAllowed.
template <typename T>
constexpr NumberResult<T> ParseInteger(std::string_view text, IntegerOptions<T> options = {}) {
	size_t start = 0;
	if (options.skipLeadingBlanks) {
		while (start < text.size() && (text[start] == ' ' || text[start] == '\t')) {
			start++;
		}
	}

	IntegerParser<T> parser(options);
	NumberError<T> rejection;
	bool rejected = false;
	for (size_t index = start; index < text.size(); index++) {
		rejection = parser.Push(text[index]);
		if (rejection.IsError()) {
			rejected = true;
			break;
		}
	}

	NumberResult<T> result = parser.Finish();
	bool noDigits = result.error.code == NumberErrorCode::Empty || result.error.code == NumberErrorCode::PrefixWithoutDigits;
	if (rejected && noDigits) {
		rejection.position += start;
		NumberResult<T> failed;
		failed.error = rejection;
		failed.endPosition = rejection.position;
		return failed;
	}

	result.endPosition += start;
	if (result.error.IsError()) {
		result.error.position += start;
	}

	return result;
}
