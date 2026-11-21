#pragma once
#include "NumberError.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>
#include <type_traits>


// Rules for reading a float. The setters return changed copies, as IntegerOptions does.
struct FloatOptions {
	bool allowPlusSign = false;
	bool skipLeadingBlanks = false;


	constexpr FloatOptions WithPlusSign() const {
		FloatOptions copy = *this;
		copy.allowPlusSign = true;
		return copy;
	}


	// Only the whole-string helper reads this: a streaming parser has no leading blanks to skip.
	constexpr FloatOptions WithLeadingBlanks() const {
		FloatOptions copy = *this;
		copy.skipLeadingBlanks = true;
		return copy;
	}
};


namespace NumberDetail {


// Unsigned integer of 1280 bits. The largest value the decimal conversion builds is 10^349 (about 1160 bits), and the long
// division keeps a few bits more, so 40 limbs never overflow. The operations change the value in place, so that the conversion
// needs no temporaries.
struct BigUnsigned {
	static constexpr size_t limbCount = 40;
	uint32_t limbs[limbCount] = {};


	constexpr void SetUint64(uint64_t value) {
		for (size_t index = 2; index < limbCount; index++) {
			limbs[index] = 0;
		}

		limbs[0] = static_cast<uint32_t>(value);
		limbs[1] = static_cast<uint32_t>(value >> 32);
	}


	constexpr void MultiplyBy(uint32_t factor) {
		uint64_t carry = 0;
		for (size_t index = 0; index < limbCount; index++) {
			uint64_t product = static_cast<uint64_t>(limbs[index]) * factor + carry;
			limbs[index] = static_cast<uint32_t>(product);
			carry = product >> 32;
		}
	}


	// Multiplies by 2^bits. The result must fit in limbCount limbs.
	constexpr void ShiftLeft(size_t bits) {
		size_t limbShift = bits / 32;
		size_t bitShift = bits % 32;
		for (size_t index = limbCount; index > 0; index--) {
			limbs[index - 1] = index > limbShift ? limbs[index - 1 - limbShift] : 0;
		}

		if (bitShift == 0) {
			return;
		}

		for (size_t index = limbCount; index > 0; index--) {
			uint32_t lower = index > 1 ? limbs[index - 2] : 0;
			limbs[index - 1] = (limbs[index - 1] << bitShift) | (lower >> (32 - bitShift));
		}
	}


	// Divides by two, dropping the lowest bit
	constexpr void ShiftRightOne() {
		for (size_t index = 0; index < limbCount; index++) {
			uint32_t upper = index + 1 < limbCount ? (limbs[index + 1] & 1u) : 0u;
			limbs[index] = (limbs[index] >> 1) | (upper << 31);
		}
	}


	constexpr void SetBit(size_t index) {
		limbs[index / 32] |= static_cast<uint32_t>(1) << (index % 32);
	}


	constexpr bool Bit(size_t index) const {
		size_t limb = index / 32;
		return limb < limbCount && ((limbs[limb] >> (index % 32)) & 1u) != 0;
	}


	// True when any bit with an index below the given one is set
	constexpr bool AnyBitBelow(size_t index) const {
		size_t fullLimbs = index / 32;
		for (size_t limb = 0; limb < fullLimbs && limb < limbCount; limb++) {
			if (limbs[limb] != 0) {
				return true;
			}
		}

		size_t partialBits = index % 32;
		if (partialBits == 0 || fullLimbs >= limbCount) {
			return false;
		}

		return (limbs[fullLimbs] & ((static_cast<uint32_t>(1) << partialBits) - 1)) != 0;
	}


	constexpr size_t BitLength() const {
		for (size_t limb = limbCount; limb > 0; limb--) {
			uint32_t value = limbs[limb - 1];
			if (value != 0) {
				size_t bits = 0;
				while (value != 0) {
					bits++;
					value >>= 1;
				}
				return (limb - 1) * 32 + bits;
			}
		}

		return 0;
	}


	// Requires other <= *this
	constexpr void Subtract(const BigUnsigned& other) {
		uint64_t borrow = 0;
		for (size_t index = 0; index < limbCount; index++) {
			uint64_t minuend = limbs[index];
			uint64_t subtrahend = static_cast<uint64_t>(other.limbs[index]) + borrow;
			if (minuend >= subtrahend) {
				limbs[index] = static_cast<uint32_t>(minuend - subtrahend);
				borrow = 0;
			} else {
				limbs[index] = static_cast<uint32_t>((static_cast<uint64_t>(1) << 32) + minuend - subtrahend);
				borrow = 1;
			}
		}
	}


	// -1, 0 or 1 as *this is below, equal to or above other
	constexpr int Compare(const BigUnsigned& other) const {
		for (size_t index = limbCount; index > 0; index--) {
			if (limbs[index - 1] != other.limbs[index - 1]) {
				return limbs[index - 1] < other.limbs[index - 1] ? -1 : 1;
			}
		}

		return 0;
	}


	constexpr bool IsZero() const {
		for (size_t index = 0; index < limbCount; index++) {
			if (limbs[index] != 0) {
				return false;
			}
		}

		return true;
	}


	// The count bits of the value shifted right by from, read as an unsigned number. A negative from shifts left, and the bits
	// that move below bit 0 are zero. The count is at most 64.
	constexpr uint64_t ExtractBitsFrom(int64_t from, size_t count) const {
		uint64_t result = 0;
		for (size_t offset = 0; offset < count; offset++) {
			int64_t index = from + static_cast<int64_t>(offset);
			if (index >= 0 && Bit(static_cast<size_t>(index))) {
				result |= static_cast<uint64_t>(1) << offset;
			}
		}

		return result;
	}
};


constexpr int64_t DecimalDigitCount(uint64_t value) {
	int64_t count = 0;
	do {
		count++;
		value /= 10;
	} while (value > 0);
	return count;
}


constexpr int64_t BitLengthOf(uint64_t value) {
	int64_t count = 0;
	while (value > 0) {
		count++;
		value >>= 1;
	}
	return count;
}


// 10^exponent, exact for the exponents the fast path uses (10^22 is the largest exact double power of ten)
template <typename T>
constexpr T PowerOfTen(int64_t exponent) {
	T power = 1;
	for (int64_t step = 0; step < exponent; step++) {
		power *= 10;
	}
	return power;
}


// A rounded binary value: significand * 2^exponent, or infinity when overflow is set. A zero has significand 0 and the exponent
// of the smallest subnormal, so the exponent always names the spacing of the floats just above the value.
struct RoundedBinary {
	uint64_t significand = 0;
	int64_t exponent = 0;
	bool overflow = false;
};


// The float nearest to mantissa * 10^exponent, rounded to nearest-even, as a significand and an exponent. The rounding is on the
// exact value of the decimal, so the only error left is the one the caller introduced by dropping digits.
template <typename T>
constexpr RoundedBinary RoundDecimal(uint64_t mantissa, int64_t exponent) {
	using Limits = std::numeric_limits<T>;
	constexpr int64_t maxDecimalExponent = 330;
	constexpr int64_t precision = Limits::digits;

	// The exponent of the smallest subnormal: the spacing of the floats below the smallest normal
	constexpr int64_t smallestExponent = Limits::min_exponent - precision;

	if (mantissa == 0) {
		return RoundedBinary{0, smallestExponent, false};
	}

	int64_t digits = DecimalDigitCount(mantissa);
	if (exponent + digits > maxDecimalExponent) {
		return RoundedBinary{0, 0, true};
	}

	if (exponent + digits < -maxDecimalExponent) {
		return RoundedBinary{0, smallestExponent, false};
	}

	// Exact big-integer path. The value is quotient * 2^-binaryShift, plus a fraction that is only known to be non-zero
	// (sticky). The division gives at least precision + 1 quotient bits. For a non-negative exponent the value is the integer
	// itself, and for a negative one the quotient is kept in the divisor's storage, after the division is done.
	BigUnsigned integer;
	BigUnsigned divisorOrQuotient;
	int64_t binaryShift = 0;
	bool sticky = false;
	if (exponent >= 0) {
		integer.SetUint64(mantissa);
		for (int64_t step = 0; step < exponent; step++) {
			integer.MultiplyBy(10);
		}
	} else {
		divisorOrQuotient.SetUint64(1);
		for (int64_t step = 0; step < -exponent; step++) {
			divisorOrQuotient.MultiplyBy(10);
		}

		integer.SetUint64(mantissa);
		binaryShift = precision + 2 + static_cast<int64_t>(divisorOrQuotient.BitLength()) - BitLengthOf(mantissa);
		if (binaryShift >= 0) {
			integer.ShiftLeft(static_cast<size_t>(binaryShift));
		} else {
			divisorOrQuotient.ShiftLeft(static_cast<size_t>(-binaryShift));
		}

		// Restoring division, from the top bit down. The divisor starts shifted to the top bit and moves one bit down each step.
		// The quotient is below 2^(precision + 3), so it fits in 64 bits.
		divisorOrQuotient.ShiftLeft(static_cast<size_t>(precision + 3));
		uint64_t bits = 0;
		for (int64_t bit = precision + 3; bit >= 0; bit--) {
			if (integer.Compare(divisorOrQuotient) >= 0) {
				integer.Subtract(divisorOrQuotient);
				bits |= static_cast<uint64_t>(1) << bit;
			}

			divisorOrQuotient.ShiftRightOne();
		}

		sticky = !integer.IsZero();
		divisorOrQuotient.SetUint64(bits);
	}

	const BigUnsigned& quotient = exponent >= 0 ? integer : divisorOrQuotient;

	// The value lies in [2^topExponent, 2^(topExponent + 1)). Round at the last bit that the format keeps: precision bits,
	// or fewer below the smallest normal number.
	int64_t topExponent = static_cast<int64_t>(quotient.BitLength()) - 1 - binaryShift;
	int64_t smallestNormal = Limits::min_exponent - 1;
	int64_t ulpExponent = std::max(topExponent, smallestNormal) - (precision - 1);
	int64_t shift = ulpExponent + binaryShift;

	uint64_t kept = quotient.ExtractBitsFrom(shift, static_cast<size_t>(precision));
	if (shift > 0) {
		bool halfBit = quotient.Bit(static_cast<size_t>(shift - 1));
		bool belowHalf = quotient.AnyBitBelow(static_cast<size_t>(shift - 1)) || sticky;
		if (halfBit && (belowHalf || (kept & 1) != 0)) {
			kept++;
		}

		// A carry out of the top bit leaves 2^precision, which is one more exponent step
		if (kept == (static_cast<uint64_t>(1) << precision)) {
			kept >>= 1;
			ulpExponent++;
		}
	}

	if (kept == 0) {
		return RoundedBinary{0, smallestExponent, false};
	}

	if (ulpExponent + BitLengthOf(kept) - 1 >= Limits::max_exponent) {
		return RoundedBinary{0, 0, true};
	}

	return RoundedBinary{kept, ulpExponent, false};
}


// The float value of a rounded binary value. The significand is below 2^precision, so each step by two is exact.
template <typename T>
constexpr T BinaryValue(const RoundedBinary& value) {
	if (value.overflow) {
		return std::numeric_limits<T>::infinity();
	}

	if (value.significand == 0) {
		return T{};
	}

	T result = static_cast<T>(value.significand);
	for (int64_t step = 0; step < value.exponent; step++) {
		result *= 2;
	}

	for (int64_t step = 0; step < -value.exponent; step++) {
		result /= 2;
	}

	return result;
}


// The float nearest to mantissa * 10^exponent. A mantissa of at most 19 digits is converted exactly with Clinger's fast path when
// the power of ten is exact, and by the big-integer path otherwise.
template <typename T>
constexpr T DecimalToBinary(uint64_t mantissa, int64_t exponent) {
	using Limits = std::numeric_limits<T>;
	constexpr int64_t precision = Limits::digits;

	if (mantissa == 0) {
		return T{};
	}

	// Clinger's fast path: an exact mantissa times or divided by an exact power of ten is one correctly rounded operation
	constexpr int64_t maxExactPower = precision == 24 ? 10 : 22;
	if (mantissa <= (static_cast<uint64_t>(1) << precision) && exponent >= -maxExactPower && exponent <= maxExactPower) {
		T scaled = static_cast<T>(mantissa);
		if (exponent >= 0) {
			return scaled * PowerOfTen<T>(exponent);
		}

		return scaled / PowerOfTen<T>(-exponent);
	}

	return BinaryValue<T>(RoundDecimal<T>(mantissa, exponent));
}


// An unsigned integer in base 10^9, least significant limb first. The midpoint between two neighbouring floats has at most 768
// significant digits, and 86 limbs hold 774.
struct BigDecimal {
	static constexpr size_t limbCount = 86;
	static constexpr uint64_t limbBase = 1000000000;
	uint32_t limbs[limbCount] = {};


	constexpr void SetValue(uint64_t value) {
		for (size_t index = 0; index < limbCount && value > 0; index++) {
			limbs[index] = static_cast<uint32_t>(value % limbBase);
			value /= limbBase;
		}
	}


	// Multiplies by a factor of at most 1.3e9, so each product fits in 64 bits
	constexpr void MultiplyBy(uint64_t factor) {
		uint64_t carry = 0;
		for (size_t index = 0; index < limbCount; index++) {
			uint64_t product = static_cast<uint64_t>(limbs[index]) * factor + carry;
			limbs[index] = static_cast<uint32_t>(product % limbBase);
			carry = product / limbBase;
		}
	}


	// The number of decimal digits, 0 for zero
	constexpr size_t DigitCount() const {
		for (size_t index = limbCount; index > 0; index--) {
			uint32_t limb = limbs[index - 1];
			if (limb != 0) {
				size_t digits = 0;
				while (limb != 0) {
					digits++;
					limb /= 10;
				}

				return (index - 1) * 9 + digits;
			}
		}

		return 0;
	}


	// The digit at a position counted from the least significant digit
	constexpr uint32_t DigitAt(size_t position) const {
		uint32_t limb = limbs[position / 9];
		for (size_t step = 0; step < position % 9; step++) {
			limb /= 10;
		}

		return limb % 10;
	}
};


// The midpoint between low and its successor, (2 * significand + 1) * 2^(exponent - 1), as midpoint * 10^midpointExponent with
// midpoint an integer. The successor is one spacing above low, so the midpoint is half a spacing above low.
constexpr void BuildMidpoint(const RoundedBinary& low, BigDecimal& midpoint, int64_t& midpointExponent) {
	midpoint.SetValue(2 * low.significand + 1);
	int64_t power = low.exponent - 1;
	if (power >= 0) {
		midpointExponent = 0;
		for (int64_t step = 0; step < power / 29; step++) {
			midpoint.MultiplyBy(static_cast<uint64_t>(1) << 29);
		}

		for (int64_t step = 0; step < power % 29; step++) {
			midpoint.MultiplyBy(2);
		}
	} else {
		// 2^power = 5^(-power) * 10^power, so the integer is odd times a power of five and the point moves by -power digits
		int64_t fives = -power;
		midpointExponent = power;
		for (int64_t step = 0; step < fives / 13; step++) {
			midpoint.MultiplyBy(1220703125);
		}

		for (int64_t step = 0; step < fives % 13; step++) {
			midpoint.MultiplyBy(5);
		}
	}
}


// Compares the decimal text with the midpoint between low and its successor, exactly. The text's first significant digit has
// position topPosition. When the midpoint's first digit has the same position, the digits are compared one by one from the top:
// the first difference decides. If the midpoint runs out first, the text is above it when it has a non-zero digit left, and
// equal otherwise. Returns -1, 0 or 1.
constexpr int CompareWithMidpoint(std::string_view accepted, int64_t topPosition, const RoundedBinary& low) {
	BigDecimal midpoint;
	int64_t midpointExponent = 0;
	BuildMidpoint(low, midpoint, midpointExponent);
	size_t midpointDigits = midpoint.DigitCount();
	int64_t midpointTop = static_cast<int64_t>(midpointDigits) - 1 + midpointExponent;
	if (topPosition != midpointTop) {
		return topPosition > midpointTop ? 1 : -1;
	}

	size_t position = 0;
	bool started = false;
	for (char character : accepted) {
		if (character == 'e' || character == 'E') {
			break;
		}

		if (character < '0' || character > '9') {
			continue;
		}

		uint32_t digit = static_cast<uint32_t>(character - '0');
		if (!started) {
			if (digit == 0) {
				continue;
			}

			started = true;
		}

		if (position < midpointDigits) {
			uint32_t expected = midpoint.DigitAt(midpointDigits - 1 - position);
			if (digit != expected) {
				return digit > expected ? 1 : -1;
			}
		} else if (digit != 0) {
			return 1;
		}

		position++;
	}

	for (size_t index = position; index < midpointDigits; index++) {
		if (midpoint.DigitAt(midpointDigits - 1 - index) != 0) {
			return -1;
		}
	}

	return 0;
}


// The float nearest to a decimal text whose mantissa keeps 19 significant digits, with the text's digits beyond them dropped. The
// value of the text lies in [mantissa, mantissa + 1) * 10^exponent. Both ends are rounded. When they agree, that value is the
// result. Otherwise at most one midpoint between floats lies in that range, and the exact text decides which side of it the value
// is on. The accepted text is read for its digits only, and the sign is not part of the magnitude.
template <typename T>
constexpr T RoundDecimalMagnitude(std::string_view accepted, uint64_t mantissa, int64_t exponent) {
	RoundedBinary low = RoundDecimal<T>(mantissa, exponent);
	RoundedBinary high = RoundDecimal<T>(mantissa + 1, exponent);
	T lowValue = BinaryValue<T>(low);
	T highValue = BinaryValue<T>(high);
	if (lowValue == highValue) {
		return lowValue;
	}

	int64_t topPosition = DecimalDigitCount(mantissa) - 1 + exponent;
	int order = CompareWithMidpoint(accepted, topPosition, low);
	if (order < 0) {
		return lowValue;
	}

	if (order > 0) {
		return highValue;
	}

	// An exact tie goes to the even significand. A zero has an even significand, and so does a finite low when high overflows.
	return low.significand % 2 == 0 ? lowValue : highValue;
}


}


// Reads a float one character at a time, with the same grammar as Print::StringToFloat: an optional sign, digits with at most
// one point, an optional exponent with its own sign, or inf and nan in any case. Push accepts or rejects a character and changes
// nothing when it rejects it. Pop undoes the last accepted character exactly.
//
// The mantissa keeps 19 significant digits. Later significant digits are counted in droppedDigits, so the value is the
// truncated mantissa, within one unit of the 19th digit of the input. Pop removes dropped digits first.
template <typename T>
class FloatParser {
	static_assert(std::is_same_v<T, float> || std::is_same_v<T, double>, "FloatParser supports float and double");


private:
	enum class SpecialKind : uint8_t {
		None,
		Infinity,
		NotANumber
	};


	static constexpr uint8_t kMaxMantissaDigits = 19;

	// An explicit exponent beyond this value is capped here. Only a text with about 4.6e18 digits could make the cap change a
	// result, and the cap keeps the sum of the exponent and the digit counts inside int64.
	static constexpr int64_t kExponentLimit = static_cast<int64_t>(1) << 62;

	FloatOptions options;
	bool negative = false;
	SpecialKind specialKind = SpecialKind::None;
	uint8_t specialLength = 0;
	uint64_t mantissa = 0;
	uint8_t storedDigits = 0;
	uint64_t droppedDigits = 0;
	uint64_t integerDigits = 0;
	uint64_t fractionDigits = 0;
	bool pointSeen = false;
	bool exponentSeen = false;
	bool exponentHasSign = false;
	bool exponentNegative = false;
	uint64_t exponentValue = 0;
	uint64_t exponentExtraDigits = 0;
	uint64_t exponentDigits = 0;
	size_t acceptedCount = 0;


public:
	constexpr explicit FloatParser(FloatOptions parserOptions = {}) : options(parserOptions) {
	}


	constexpr NumberError<T> Push(char character) {
		if (acceptedCount == 0 && (character == '+' || character == '-')) {
			if (character == '+' && !options.allowPlusSign) {
				return MakeError(NumberErrorCode::SignNotAllowed);
			}

			negative = character == '-';
			acceptedCount++;
			return NumberError<T>{};
		}

		if (specialKind != SpecialKind::None) {
			return PushSpecialLetter(character);
		}

		if (exponentSeen) {
			return PushExponentCharacter(character);
		}

		if (character >= '0' && character <= '9') {
			AppendMantissaDigit(static_cast<uint8_t>(character - '0'));
			return NumberError<T>{};
		}

		if (character == '.' && !pointSeen) {
			pointSeen = true;
			acceptedCount++;
			return NumberError<T>{};
		}

		if ((character == 'e' || character == 'E') && MantissaDigitCount() > 0) {
			exponentSeen = true;
			acceptedCount++;
			return NumberError<T>{};
		}

		if (NoBodyYet() && (character == 'i' || character == 'I')) {
			specialKind = SpecialKind::Infinity;
			specialLength = 1;
			acceptedCount++;
			return NumberError<T>{};
		}

		if (NoBodyYet() && (character == 'n' || character == 'N')) {
			specialKind = SpecialKind::NotANumber;
			specialLength = 1;
			acceptedCount++;
			return NumberError<T>{};
		}

		return MakeError(NumberErrorCode::InvalidCharacter);
	}


	// Undoes the last accepted character. Does nothing on an empty parser.
	constexpr void Pop() {
		if (acceptedCount == 0) {
			return;
		}

		acceptedCount--;

		if (specialKind != SpecialKind::None) {
			specialLength--;
			if (specialLength == 0) {
				specialKind = SpecialKind::None;
			}
			return;
		}

		if (exponentSeen) {
			PopExponentCharacter();
			return;
		}

		if (pointSeen && fractionDigits > 0) {
			fractionDigits--;
			RemoveMantissaDigit();
			return;
		}

		if (pointSeen) {
			pointSeen = false;
			return;
		}

		if (integerDigits > 0) {
			integerDigits--;
			RemoveMantissaDigit();
			return;
		}

		negative = false;
	}


	// True after an exponent marker that has no digit yet. The whole-string helper pops such a marker.
	constexpr bool IsExponentPending() const {
		return exponentSeen && exponentDigits == 0;
	}


	// The first 19 significant digits of the text, and the decimal exponent that goes with them: the value lies in
	// [StoredMantissa(), StoredMantissa() + 1) * 10^MantissaExponent(). The whole-string helper reads these.
	constexpr uint64_t StoredMantissa() const {
		return mantissa;
	}


	constexpr int64_t MantissaExponent() const {
		return DecimalExponent();
	}


	// True when a significant digit beyond the 19 kept ones was read. The mantissa then only bounds the value.
	constexpr bool HasDroppedDigits() const {
		return droppedDigits > 0;
	}


	constexpr NumberResult<T> Finish() const {
		NumberResult<T> result;
		result.endPosition = acceptedCount;

		if (specialKind != SpecialKind::None) {
			if (specialLength < 3) {
				result.error = MakeError(NumberErrorCode::InvalidCharacter);
				return result;
			}

			if (specialKind == SpecialKind::Infinity) {
				result.value = negative ? -std::numeric_limits<T>::infinity() : std::numeric_limits<T>::infinity();
			} else {
				result.value = std::numeric_limits<T>::quiet_NaN();
			}

			return result;
		}

		if (MantissaDigitCount() == 0) {
			result.error = MakeError(NumberErrorCode::Empty);
			return result;
		}

		if (exponentSeen && exponentDigits == 0) {
			result.error = MakeError(NumberErrorCode::ExponentWithoutDigits);
			return result;
		}

		T magnitude = NumberDetail::DecimalToBinary<T>(mantissa, DecimalExponent());
		result.value = negative ? -magnitude : magnitude;
		return result;
	}


private:
	constexpr NumberError<T> MakeError(NumberErrorCode code) const {
		NumberError<T> error;
		error.code = code;
		error.position = acceptedCount;
		return error;
	}


	constexpr uint64_t MantissaDigitCount() const {
		return integerDigits + fractionDigits;
	}


	// True while nothing but an optional sign has been accepted, the only place where inf and nan may start
	constexpr bool NoBodyYet() const {
		return MantissaDigitCount() == 0 && !pointSeen && !exponentSeen;
	}


	constexpr NumberError<T> PushSpecialLetter(char character) {
		const char* word = specialKind == SpecialKind::Infinity ? "inf" : "nan";
		char lower = (character >= 'A' && character <= 'Z') ? static_cast<char>(character + ('a' - 'A')) : character;
		if (specialLength < 3 && lower == word[specialLength]) {
			specialLength++;
			acceptedCount++;
			return NumberError<T>{};
		}

		return MakeError(NumberErrorCode::InvalidCharacter);
	}


	constexpr NumberError<T> PushExponentCharacter(char character) {
		if (character >= '0' && character <= '9') {
			AppendExponentDigit(static_cast<uint8_t>(character - '0'));
			return NumberError<T>{};
		}

		// A sign is only valid straight after the marker
		if ((character == '+' || character == '-') && exponentDigits == 0 && !exponentHasSign) {
			exponentHasSign = true;
			exponentNegative = character == '-';
			acceptedCount++;
			return NumberError<T>{};
		}

		return MakeError(NumberErrorCode::InvalidCharacter);
	}


	constexpr void AppendMantissaDigit(uint8_t digit) {
		acceptedCount++;
		if (pointSeen) {
			fractionDigits++;
		} else {
			integerDigits++;
		}

		// A zero before the first significant digit only moves the point
		if (storedDigits == 0 && digit == 0) {
			return;
		}

		if (storedDigits < kMaxMantissaDigits) {
			mantissa = mantissa * 10 + digit;
			storedDigits++;
		} else {
			droppedDigits++;
		}
	}


	constexpr void RemoveMantissaDigit() {
		if (droppedDigits > 0) {
			droppedDigits--;
			return;
		}

		if (storedDigits > 0) {
			mantissa /= 10;
			storedDigits--;
		}
	}


	constexpr void AppendExponentDigit(uint8_t digit) {
		acceptedCount++;
		exponentDigits++;
		if (exponentExtraDigits > 0 || exponentValue > (std::numeric_limits<uint64_t>::max() - digit) / 10) {
			exponentExtraDigits++;
		} else {
			exponentValue = exponentValue * 10 + digit;
		}
	}


	constexpr void PopExponentCharacter() {
		if (exponentDigits > 0) {
			exponentDigits--;
			if (exponentExtraDigits > 0) {
				exponentExtraDigits--;
			} else {
				exponentValue /= 10;
			}
			return;
		}

		if (exponentHasSign) {
			exponentHasSign = false;
			exponentNegative = false;
			return;
		}

		exponentSeen = false;
	}


	// The decimal exponent of the mantissa: each fraction digit lowers it, each dropped digit raises it
	constexpr int64_t DecimalExponent() const {
		int64_t explicitExponent = (exponentExtraDigits > 0 || exponentValue > static_cast<uint64_t>(kExponentLimit))
			? kExponentLimit
			: static_cast<int64_t>(exponentValue);
		if (exponentNegative) {
			explicitExponent = -explicitExponent;
		}

		return static_cast<int64_t>(droppedDigits) - static_cast<int64_t>(fractionDigits) + explicitExponent;
	}
};


namespace NumberDetail {


// Pushes the text, from start on, through a streaming parser. Positions are absolute. The parser's mantissa and decimal exponent
// are returned, and droppedDigits says whether the exact rounding is needed.
template <typename T>
constexpr NumberResult<T> StreamWholeText(std::string_view text, size_t start, FloatOptions options, uint64_t& mantissa,
	int64_t& decimalExponent, bool& droppedDigits) {
	FloatParser<T> parser(options);
	NumberError<T> rejection;
	bool rejected = false;
	for (size_t index = start; index < text.size(); index++) {
		rejection = parser.Push(text[index]);
		if (rejection.IsError()) {
			rejected = true;
			break;
		}
	}

	while (parser.IsExponentPending()) {
		parser.Pop();
	}

	NumberResult<T> result = parser.Finish();

	// No digit was read: the character that stopped the text says why, as it does for integers
	if (rejected && result.error.code == NumberErrorCode::Empty) {
		rejection.position += start;
		NumberResult<T> failed;
		failed.error = rejection;
		failed.endPosition = rejection.position;
		return failed;
	}

	mantissa = parser.StoredMantissa();
	decimalExponent = parser.MantissaExponent();
	droppedDigits = parser.HasDroppedDigits();
	result.endPosition += start;
	if (result.error.IsError()) {
		result.error.position += start;
	}

	return result;
}


}


// Reads the longest prefix of text that the grammar accepts, as Print::StringToFloat does: "1.5abc" gives 1.5 and an end
// position of 3. An exponent marker with no digits is not read: "1e" gives 1 and an end position of 1.
//
// The value is correctly rounded (round half to even) for any number of digits. The streaming parser keeps 19 digits, so for
// a text with more digits this helper compares the whole text with the midpoints that could decide the rounding.
template <typename T>
constexpr NumberResult<T> ParseFloat(std::string_view text, FloatOptions options = {}) {
	size_t start = 0;
	if (options.skipLeadingBlanks) {
		while (start < text.size() && (text[start] == ' ' || text[start] == '\t')) {
			start++;
		}
	}

	uint64_t mantissa = 0;
	int64_t decimalExponent = 0;
	bool droppedDigits = false;
	NumberResult<T> result = NumberDetail::StreamWholeText<T>(text, start, options, mantissa, decimalExponent, droppedDigits);

	// Digits were dropped only when the text has more than 19 significant digits. Then the whole accepted text decides.
	if (result.IsOk() && droppedDigits) {
		std::string_view accepted = text.substr(start, result.endPosition - start);
		T magnitude = NumberDetail::RoundDecimalMagnitude<T>(accepted, mantissa, decimalExponent);
		bool isNegative = !accepted.empty() && accepted.front() == '-';
		result.value = isNegative ? -magnitude : magnitude;
	}

	return result;
}
