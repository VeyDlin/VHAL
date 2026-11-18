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


// Angle constants in Q32 radians. 2*pi is rounded to 32 fractional bits, so each turn adds at most 2^-33 rad of error.
// The reduction stays in Q32 because a reduced angle held in Q bits is off by up to one LSB of the format.
constexpr int64_t twoPi32 = static_cast<int64_t>(6.283185307179586 * 4294967296.0 + 0.5);
constexpr int64_t pi32 = twoPi32 / 2;
constexpr int64_t halfPi32 = twoPi32 / 4;


// Lookup sin from quarter-wave table with linear interpolation. The angle is in Q32 radians and any 64-bit value is accepted.
template<int Q, int L>
IQ<Q, L> SinLookupQ32(int64_t angle32) {
	const SinLUT<L>& lut = sinLut<L>;

	// Reduce to [0, 2*pi)
	int64_t a = angle32 % twoPi32;
	if (a < 0) {
		a += twoPi32;
	}

	// Quadrant reduction to [0, pi/2]: sin(a + pi) = -sin(a) and sin(pi - a) = sin(a)
	bool negate = false;
	if (a >= pi32) {
		a -= pi32;
		negate = true;
	}
	if (a > halfPi32) {
		a = pi32 - a;
	}

	// Table index and the fraction between two entries; the remainder is below halfPi32 < 2^33
	int64_t scaled = a * L;
	int64_t idx = scaled / halfPi32;
	int64_t remainder = scaled - idx * halfPi32;

	int64_t v0 = lut.table[idx];
	int64_t v1 = lut.table[idx < L ? idx + 1 : idx];

	// Weight in Q30; (remainder << 30) < 2^63 and (v1 - v0) * weight < 2^61
	int64_t weight = (remainder << 30) / halfPi32;
	int64_t value31 = v0 + (((v1 - v0) * weight) >> 30);

	// Q31 to Q, truncating toward zero (the value is not negative here)
	int64_t result = value31 >> (31 - Q);

	return IQ<Q, L>::FromRaw(static_cast<int32_t>(negate ? -result : result));
}

}


// ADL-compatible free functions
template<int Q, int L>
IQ<Q, L> sin(IQ<Q, L> x) {
	return IQDetail::SinLookupQ32<Q, L>(static_cast<int64_t>(x.raw) * (int64_t(1) << (32 - Q)));
}

template<int Q, int L>
IQ<Q, L> cos(IQ<Q, L> x) {
	return IQDetail::SinLookupQ32<Q, L>(static_cast<int64_t>(x.raw) * (int64_t(1) << (32 - Q)) + IQDetail::halfPi32);
}
