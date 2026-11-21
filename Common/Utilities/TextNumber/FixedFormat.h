#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>


// Fixed-point text with no exponent, as JSON writes a number: precision digits after the point, clamped to 0..9, rounded half
// away from zero on the scaled value, and a minus sign only when the rounded result is not zero. Returns the number of
// characters, or 0 and writes nothing when the value is NaN or infinite, when its scaled integer would reach 9.0e18, or when
// the text does not fit. Constexpr: the rounding is done by hand, so no libm function is needed.
constexpr size_t FormatFixedPoint(std::span<char> output, double value, int precision) {
	constexpr double maxDouble = std::numeric_limits<double>::max();
	if (value != value || value > maxDouble || value < -maxDouble) {
		return 0;
	}

	size_t digits = static_cast<size_t>(std::clamp(precision, 0, 9));
	uint64_t scale = 1;
	for (size_t index = 0; index < digits; ++index) {
		scale *= 10;
	}

	double magnitude = value < 0.0 ? -value : value;
	double scaled = magnitude * static_cast<double>(scale);
	if (scaled >= 9.0e18) {
		return 0;
	}

	// Rounds half away from zero, the same result as std::llround on a non-negative value
	uint64_t rounded = static_cast<uint64_t>(scaled);
	if (scaled - static_cast<double>(rounded) >= 0.5) {
		rounded++;
	}

	uint64_t integerPart = rounded / scale;
	uint64_t fractionPart = rounded % scale;

	// Digits are produced least significant first and copied out in reverse.
	char reversed[32] = {};
	size_t count = 0;
	for (size_t index = 0; index < digits; ++index) {
		reversed[count++] = static_cast<char>('0' + fractionPart % 10);
		fractionPart /= 10;
	}

	if (digits > 0) {
		reversed[count++] = '.';
	}

	do {
		reversed[count++] = static_cast<char>('0' + integerPart % 10);
		integerPart /= 10;
	} while (integerPart > 0);

	if (value < 0.0 && rounded != 0) {
		reversed[count++] = '-';
	}

	if (count > output.size()) {
		return 0;
	}

	for (size_t index = 0; index < count; ++index) {
		output[index] = reversed[count - 1 - index];
	}

	return count;
}
