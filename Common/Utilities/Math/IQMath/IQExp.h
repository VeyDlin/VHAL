#pragma once
#include "IQ.h"


namespace IQDetail {
	// exp and expm1 compute in 30 fraction bits, whatever Q is. The argument is split as x = k ln2 + r with |r| about
	// 0.35 or less. The series for exp(r) - 1 is evaluated in those bits and the result is 2^k (1 + series). The error
	// before the final rounding to Q bits is a few units of 2^-30 relative to the value, so near the top of the range it
	// is a few LSB for every Q.
	constexpr int expFraction = 30;
	constexpr int64 expOne = int64(1) << expFraction;
	// ln 2 rounded to 30 fraction bits; the rounding error is -0.045 units, kept below in units of 2^-60.
	constexpr int64 expLn2 = 744261118;
	constexpr int64 expLn2Low = -48433253;
	// Beyond |x| = 23 the result is saturated (above) or rounds to 0 or -1 (below) in every Q.
	constexpr int64 expLimit = 23 * expOne;
	// Marks a result above every IQ value; any value shifted right by at most 29 still saturates.
	constexpr int64 expOverflow = int64(1) << 62;


	// Coefficients 1 / n! for n = 1 to 11, rounded to 30 fraction bits, built at compile time.
	struct ExpSeries {
		int64 coefficient[12] = {};

		constexpr ExpSeries() {
			int64 factorial = 1;
			for (int n = 1; n <= 11; n++) {
				factorial *= n;
				coefficient[n] = (expOne + factorial / 2) / factorial;
			}
		}
	};

	inline constexpr ExpSeries expSeries{};


	// Product of two values with 30 fraction bits, rounded to nearest.
	constexpr int64 MultiplyExp(int64 a, int64 b) {
		return (a * b + (expOne >> 1)) >> expFraction;
	}

	// Division by 2^shift for shift >= 1, rounded to nearest. A tie goes toward +infinity, also for negative values, which
	// is the opposite of the construction from float (ties away from zero).
	constexpr int64 ShiftRightRounded(int64 value, int shift) {
		return (value + (int64(1) << (shift - 1))) >> shift;
	}

	// exp(x) for x in 30 fraction bits, as a value in 30 fraction bits. Overflow gives expOverflow and underflow gives 0.
	constexpr int64 ExpScaled(int64 x) {
		if (x > expLimit) {
			return expOverflow;
		}
		if (x < -expLimit) {
			return 0;
		}

		// k is the integer nearest to x / ln2, so the remainder r lies within about 0.35 of zero.
		int64 half = expLn2 / 2;
		int64 k = x >= 0 ? (x + half) / expLn2 : -((-x + half) / expLn2);
		int64 r = x - k * expLn2 - ShiftRightRounded(k * expLn2Low, expFraction);

		// Horner form of S = r (c1 + r (c2 + ... + r c11)) with c_n = 1 / n!, so S = exp(r) - 1.
		int64 accumulator = expSeries.coefficient[11];
		for (int n = 10; n >= 1; n--) {
			accumulator = expSeries.coefficient[n] + MultiplyExp(r, accumulator);
		}
		int64 scaled = expOne + MultiplyExp(r, accumulator);

		if (k >= 32) {
			return expOverflow;
		}
		if (k >= 0) {
			return scaled << k;
		}
		return ShiftRightRounded(scaled, static_cast<int>(-k));
	}

	// The IQ raw value of a 30-bit value, rounded to nearest and saturated.
	template<int Q>
	constexpr int32 FromExpFraction(int64 value) {
		if constexpr (Q == expFraction) {
			return SaturateRaw(value);
		} else {
			return SaturateRaw(ShiftRightRounded(value, expFraction - Q));
		}
	}

	// The raw value of an IQ argument in 30 fraction bits.
	template<int Q>
	constexpr int64 ToExpFraction(int32 raw) {
		return static_cast<int64>(raw) << (expFraction - Q);
	}
}


// exp(x) = 2^k (1 + series). Near zero it keeps the precision of the series instead of rounding 1 + small values first.
// The worst error against the exact result, over every raw value, is below 2.2 LSB for Q up to 28 and below 3.3 LSB at
// Q30 (the table in the README of IQMath).
template<int Q, int L>
constexpr IQ<Q, L> exp(IQ<Q, L> x) {
	return IQ<Q, L>::FromRaw(IQDetail::FromExpFraction<Q>(IQDetail::ExpScaled(IQDetail::ToExpFraction<Q>(x.raw))));
}


// expm1(x) = exp(x) - 1, accurate for small arguments: the series is not rounded to one before the subtraction.
template<int Q, int L>
constexpr IQ<Q, L> expm1(IQ<Q, L> x) {
	return IQ<Q, L>::FromRaw(IQDetail::FromExpFraction<Q>(IQDetail::ExpScaled(IQDetail::ToExpFraction<Q>(x.raw)) - IQDetail::expOne));
}
