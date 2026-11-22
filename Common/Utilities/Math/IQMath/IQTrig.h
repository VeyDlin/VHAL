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


// log2 of a power of two, for the shifts that split a phase into a table index and a remainder.
constexpr int Log2Exact(int value) {
	int bits = 0;
	while ((1 << bits) < value) {
		bits++;
	}
	return bits;
}


// Radians to phase for raw IQ<Q> values: phase = raw * 2^(32 - Q) / (2 pi) = raw * M / 2^(Q + 2), with M = 2^33 / pi
// rounded to the nearest integer. M is 2734261102 (below 2^32), so one product fits in 64 bits for every int32 raw.
// The rounding of M moves the phase by at most |raw| * 0.5 / 2^(Q + 2) <= 2^(28 - Q) units, which is 2 pi / 16 = 0.393
// LSB of Q at every Q. The final shift rounds to a unit and adds at most 0.5 unit more (about 0.012 LSB at Q24).
constexpr int64 radianMultiplier = static_cast<int64>(8589934592.0 / 3.14159265358979323846 + 0.5);

template<int Q>
constexpr uint32 RadianRawToPhase(int32 raw) {
	int64 product = static_cast<int64>(raw) * radianMultiplier + (int64(1) << (Q + 1));
	return static_cast<uint32>(product >> (Q + 2));
}

// 2 pi, and 1/6 in Q18 for the cubic term of the sine. The rounding of 1/6 is 1e-5 relative, far below one LSB of Q24.
constexpr double twoPiValue = 6.283185307179586476925286766559;
constexpr int64 sixthQ18 = static_cast<int64>(262144.0 / 6.0 + 0.5);


// Sine of a binary angle in Q with table size L, rounded to the nearest LSB.
//
// The quadrant comes from the top two bits. An odd quadrant uses sin(pi/2 - x) = cos(x), so its offset is mirrored to
// (0, 2^30], and the table index stays in [0, L]. The distance t from the table point is below 2 pi / (4 L), which is
// 2 pi / 1024 for L = 256. With the table point a, sin(a + t) = sin(a) (1 - t^2 / 2) + cos(a) (t - t^3 / 6). The next
// term, t^4 / 24, is below 2^-33 for L = 256. sin(a) and cos(a) both come from the quarter table: cos(a) = sin(pi/2 - a)
// is entry L - index.
// The sum is in Q30 with 64-bit products taken as their high words. The error is the table rounding plus the final
// rounding. A radian argument adds the phase error described at RadianRawToPhase.
// The error is at most 1 LSB up to Q24 when L >= 64 (see the README). Smaller tables lack the t^4 / 24 correction:
// L = 32 gives 4.5 LSB at Q24 and L = 16 gives 65 LSB. Above Q24 the error grows with Q, because the phase unit is
// 2 pi / 2^32 rad.
template<int Q, int L>
constexpr IQ<Q, L> SinPhaseCore(uint32 phase) {
	constexpr int indexShift = 30 - Log2Exact(L);
	constexpr int64 twoPiScaled = static_cast<int64>(twoPiValue * static_cast<double>(int64(1) << indexShift) + 0.5);
	const SinLUT<L>& lut = sinLut<L>;

	uint32 quadrant = phase >> 30;
	uint32 offset = phase & 0x3FFFFFFFu;
	if ((quadrant & 1u) != 0) {
		offset = 0x40000000u - offset;
	}

	// The table is Q31. The sum is kept in Q30, one bit below the table, so that it cannot overflow int32 near 1.
	uint32 index = offset >> indexShift;
	int64 sinA = lut.table[index] >> 1;
	int64 cosA = lut.table[L - index] >> 1;

	// The distance t from the table point, in Q32 (t * 2^32 = remainder * 2 pi). The remainder moves to the top bits, so
	// the high word of the product is the scaled distance. All later products take their high word the same way.
	uint32 remainderTop = offset << (32 - indexShift);
	int64 tq = (static_cast<int64>(remainderTop) * twoPiScaled) >> 32;
	int64 t2 = (tq * tq) >> 32;
	int64 t3 = (t2 * tq) >> 32;

	// Q30: sin(a) (1 - t^2 / 2) + cos(a) (t - t^3 / 6). Every term is at most 2^30 in magnitude, so the sum fits in 32
	// bits. The cubic product is kept in 64 bits, because it is large for small L.
	int32 sinTerm = static_cast<int32>((sinA * (t2 >> 1)) >> 32);
	int32 cosTerm = static_cast<int32>((cosA * tq) >> 32);
	int32 cubicTerm = static_cast<int32>((((cosA * t3) >> 32) * sixthQ18) >> 18);
	int32 value = static_cast<int32>(sinA) - cubicTerm - sinTerm + cosTerm;

	// Upper half of the circle: the sign flips
	int32 signedValue = (quadrant & 2u) != 0 ? -value : value;

	// Q30 to Q, rounding half up: floor(v / 2^s) plus bit s - 1 of v, with s = 30 - Q. Unlike (v + 2^(s - 1)) >> s this
	// cannot overflow. For Q = 30 the value is already the raw value.
	if constexpr (Q == 30) {
		return IQ<Q, L>::FromRaw(signedValue);
	} else {
		int32 raw = (signedValue >> (30 - Q)) + ((signedValue >> (29 - Q)) & 1);
		return IQ<Q, L>::FromRaw(raw);
	}
}

}


// Converts an angle in radians to a binary phase, for constants and for angles of any size. The angle is taken in turns
// first, so the fraction of a turn is exact and only the division by 2 pi rounds (a relative 2^-53). The result is rounded
// to the nearest unit (2 pi / 2^32 rad) and wraps to one turn, so 2 pi gives 0. Beyond 2^52 turns a double has no
// fraction, so the phase is 0; NaN and infinity give 0 too.
constexpr uint32 RadiansToPhase(double radians) {
	constexpr double fractionLimit = 4503599627370496.0; // 2^52

	const double turns = radians / IQDetail::twoPiValue;
	double fraction = 0.0;
	if (turns > -fractionLimit && turns < fractionLimit) {
		fraction = turns - static_cast<double>(static_cast<int64>(turns));
	}

	// One turn is 2^32 units, so the fraction is in (-1, 1) and the units fit in int64 with room to spare.
	const double units = fraction * 4294967296.0;
	const int64 whole = static_cast<int64>(units >= 0.0 ? units + 0.5 : units - 0.5);
	return static_cast<uint32>(whole);
}


// Sine and cosine of a binary angle (uint32, 2^32 is one turn) with the default table size. Integer code only: no
// division, no call, and the uint32 wrap does the reduction.
template<int Q>
constexpr IQ<Q> SinPhase(uint32 phase) {
	return IQDetail::SinPhaseCore<Q, IQ<Q>::lutSize>(phase);
}

template<int Q>
constexpr IQ<Q> CosPhase(uint32 phase) {
	return IQDetail::SinPhaseCore<Q, IQ<Q>::lutSize>(phase + 0x40000000u);
}


// ADL-compatible free functions. The argument is in radians; cos(x) is the sine of the phase a quarter turn ahead.
template<int Q, int L>
constexpr IQ<Q, L> sin(IQ<Q, L> x) {
	return IQDetail::SinPhaseCore<Q, L>(IQDetail::RadianRawToPhase<Q>(x.raw));
}

template<int Q, int L>
constexpr IQ<Q, L> cos(IQ<Q, L> x) {
	return IQDetail::SinPhaseCore<Q, L>(IQDetail::RadianRawToPhase<Q>(x.raw) + 0x40000000u);
}
