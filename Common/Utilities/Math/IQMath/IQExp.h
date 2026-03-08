#pragma once
#include "IQ.h"


// exp(x) via Taylor + range reduction: exp(x) = 2^k * exp(r), |r| <= ln2/2
template<int Q, int L>
IQ<Q, L> exp(IQ<Q, L> x) {
	constexpr IQ<Q, L> one(1.0f);
	constexpr IQ<Q, L> ln2(0.6931471805599453f);
	constexpr IQ<Q, L> inv_ln2(1.4426950408889634f);

	// Clamp to avoid overflow
	constexpr int max_int = 31 - Q;
	constexpr IQ<Q, L> max_x(static_cast<float>(max_int) * 0.6931471805599453f);
	if (x > max_x) x = max_x;
	if (x < -max_x) return IQ<Q, L>(0.0f);

	// Range reduction: x = k*ln2 + r
	IQ<Q, L> kf = x * inv_ln2;
	int32_t k = kf.ToInt();
	IQ<Q, L> r = x - IQ<Q, L>(static_cast<float>(k)) * ln2;

	// Taylor: exp(r) ≈ 1 + r + r^2/2 + r^3/6 + r^4/24 + r^5/120
	IQ<Q, L> r2 = r * r;
	IQ<Q, L> r3 = r2 * r;
	IQ<Q, L> r4 = r3 * r;

	constexpr IQ<Q, L> c2(0.5f);
	constexpr IQ<Q, L> c3(0.166666667f);
	constexpr IQ<Q, L> c4(0.041666667f);
	constexpr IQ<Q, L> c5(0.008333333f);

	IQ<Q, L> result = one + r + c2 * r2 + c3 * r3 + c4 * r4 + c5 * r4 * r;

	// Multiply by 2^k via shift
	if (k >= 0) {
		if (k < 31) result.raw <<= k;
		else result.raw = 0x7FFFFFFF;
	} else {
		if (-k < 31) result.raw >>= (-k);
		else result.raw = 0;
	}

	return result;
}
