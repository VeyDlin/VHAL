#pragma once
#include "IQ.h"


namespace IQDetail {

// Polynomial approximation of atan(x) for x in [0, 1]
// 9th order minimax, max error ~0.0005 rad
template<int Q, int L>
IQ<Q, L> AtanApprox(IQ<Q, L> x) {
	constexpr IQ<Q, L> a1( 0.9998660f);
	constexpr IQ<Q, L> a3(-0.3302995f);
	constexpr IQ<Q, L> a5( 0.1801410f);
	constexpr IQ<Q, L> a7(-0.0851330f);
	constexpr IQ<Q, L> a9( 0.0208351f);

	IQ<Q, L> x2 = x * x;
	IQ<Q, L> x3 = x2 * x;
	IQ<Q, L> x5 = x3 * x2;
	IQ<Q, L> x7 = x5 * x2;
	IQ<Q, L> x9 = x7 * x2;

	return a1 * x + a3 * x3 + a5 * x5 + a7 * x7 + a9 * x9;
}

} 


template<int Q, int L>
IQ<Q, L> atan2(IQ<Q, L> y, IQ<Q, L> x) {
	constexpr IQ<Q, L> pi(3.14159265358979f);
	constexpr IQ<Q, L> half_pi(1.57079632679490f);
	constexpr IQ<Q, L> zero(0.0f);

	if (x.raw == 0 && y.raw == 0) return zero;

	if (x.raw == 0) {
		return y.raw > 0 ? half_pi : -half_pi;
	}

	if (y.raw == 0) {
		return x.raw > 0 ? zero : pi;
	}

	IQ<Q, L> ax = abs(x);
	IQ<Q, L> ay = abs(y);

	IQ<Q, L> angle;
	if (ay <= ax) {
		// |y/x| <= 1, direct atan
		IQ<Q, L> ratio = ay / ax;
		angle = IQDetail::AtanApprox(ratio);
	} else {
		// |y/x| > 1, use atan(z) = pi/2 - atan(1/z)
		IQ<Q, L> ratio = ax / ay;
		angle = half_pi - IQDetail::AtanApprox(ratio);
	}

	// Map to correct quadrant
	if (x.raw < 0) angle = pi - angle;
	if (y.raw < 0) angle = -angle;

	return angle;
}
