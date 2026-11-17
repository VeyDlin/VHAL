#pragma once
#include "IQ.h"


namespace IQDetail {

constexpr double TaylorSin(double x) {
	double x2 = x * x;
	double term = x;
	double sum = x;

	term *= -x2 / (2.0 * 3.0);
	sum += term;
	term *= -x2 / (4.0 * 5.0);
	sum += term;
	term *= -x2 / (6.0 * 7.0);
	sum += term;
	term *= -x2 / (8.0 * 9.0);
	sum += term;
	term *= -x2 / (10.0 * 11.0);
	sum += term;
	term *= -x2 / (12.0 * 13.0);
	sum += term;

	return sum;
}


// Compile-time LUT generation for sin (quarter wave, [0, pi/2])
// Stores values in Q31 format for maximum precision
template<int Size>
struct SinLUT {
	static_assert((Size & (Size - 1)) == 0, "Size must be a power of 2");

	int32_t table[Size + 1] = {};

	constexpr SinLUT() {
		constexpr double pi_half = 1.5707963267948966;
		for (int i = 0; i < Size; i++) {
			double angle = pi_half * i / Size;
			double s = TaylorSin(angle);
			if (s > 1.0) s = 1.0;
			if (s < -1.0) s = -1.0;
			table[i] = static_cast<int32_t>(s * 2147483647.0); // Q31
		}
		table[Size] = 2147483647; // sin(pi/2) = 1.0 exactly
	}
};


// Template variable — single instance per LUT size
template<int Size>
inline constexpr SinLUT<Size> sinLut{};


// Lookup sin from quarter-wave table with linear interpolation.
// The angle has 32 fractional bits: a 2*pi constant kept at Q bits loses about one LSB per turn, which is visible for large angles.
template<int Q, int L>
IQ<Q, L> SinLookupQ32(int64_t angle32) {
	const auto& lut = sinLut<L>;

	constexpr int64_t two_pi32 = static_cast<int64_t>(6.283185307179586 * 4294967296.0);
	constexpr int32_t pi_raw = static_cast<int32_t>(3.141592653589793 * (int32_t(1) << Q));
	constexpr int32_t half_pi_raw = static_cast<int32_t>(1.5707963267948966 * (int32_t(1) << Q));

	// Reduce to [0, 2*pi) in Q32, then drop to Q
	angle32 = angle32 % two_pi32;
	if (angle32 < 0) angle32 += two_pi32;
	int32_t a = static_cast<int32_t>(angle32 >> (32 - Q));

	// Determine quadrant and reduce to [0, pi/2]
	bool negate = false;
	if (a >= pi_raw) {
		a -= pi_raw;
		negate = true;
	}
	if (a > half_pi_raw) {
		a = pi_raw - a;
	}

	// Map to table index [0, L]
	int64_t idx_full = static_cast<int64_t>(a) * L / half_pi_raw;
	int32_t idx = static_cast<int32_t>(idx_full);
	if (idx >= L) idx = L;

	// Linear interpolation
	int32_t v0 = lut.table[idx];
	int32_t v1 = lut.table[idx < L ? idx + 1 : idx];
	int64_t frac_num = static_cast<int64_t>(a) * L - static_cast<int64_t>(idx) * half_pi_raw;
	int64_t interp = v0 + (static_cast<int64_t>(v1 - v0) * frac_num) / half_pi_raw;

	// Convert from Q31 to IQ<Q>
	int32_t result;
	if constexpr (Q <= 31) {
		result = static_cast<int32_t>(interp >> (31 - Q));
	} else {
		result = static_cast<int32_t>(interp << (Q - 31));
	}

	return IQ<Q, L>::FromRaw(negate ? -result : result);
}

}


// ADL-compatible free functions
template<int Q, int L>
IQ<Q, L> sin(IQ<Q, L> x) {
	return IQDetail::SinLookupQ32<Q, L>(static_cast<int64_t>(x.raw) * (int64_t(1) << (32 - Q)));
}

template<int Q, int L>
IQ<Q, L> cos(IQ<Q, L> x) {
	constexpr int64_t half_pi32 = static_cast<int64_t>(1.5707963267948966 * 4294967296.0);
	return IQDetail::SinLookupQ32<Q, L>(static_cast<int64_t>(x.raw) * (int64_t(1) << (32 - Q)) + half_pi32);
}
