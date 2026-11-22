#pragma once
#include <Utilities/DataTypes.h>


// Float sine and cosine without libm. This is opt-in: std::sin and std::cos stay the default. The argument is in radians.
// The argument is reduced by k * pi/2 with a Cody-Waite split of pi/2 into three parts, so the reduced angle is in
// [-pi/4, pi/4] (plus a rounding of the last digit), and then a Taylor polynomial gives sin or cos. The reduction is exact
// for |x| below about 2.5e4; the accuracy is measured in TestFastTrig.cpp over [-1e4, 1e4] and over [1e4, 2.5e4].
// Beyond about 1.6e6 the argument is clamped before the reduction: the result is in [-1, 1] but is not the sine of x.
// NaN and infinity give NaN.
namespace FastTrigDetail {

// Largest argument that is reduced. Its quotient by pi/2 is about 1e6, which keeps the int32 conversion defined.
constexpr float maxArgument = 1.6e6f;

// pi/2 in three parts, then 2/pi for the quotient. The first part has 8 significant bits and the second 10, so
// k * first and k * second are exact for |k| below 2^14. The third is the rest of pi/2, rounded to float, and its product
// with k is below 1e-3.
constexpr float halfPiHigh = 1.5703125f;
constexpr float halfPiMiddle = 1015.0f / 2097152.0f;
constexpr float halfPiLow = -1.6292068e-7f;
constexpr float twoOverPi = 0.63661977236758134f;


struct ReducedAngle {
	float remainder;
	int32 quadrant;
};


// The nearest multiple k of pi/2 gives the remainder x - k pi/2 and the quadrant k mod 4. NaN and infinity have no
// nearest multiple: their remainder is NaN, which the polynomials carry to the result.
inline ReducedAngle ReduceHalfPi(float x) {
	if (x - x != 0.0f) {
		return ReducedAngle{x - x, 0};
	}

	float clamped = x;
	if (clamped > maxArgument) {
		clamped = maxArgument;
	}
	if (clamped < -maxArgument) {
		clamped = -maxArgument;
	}

	float scaled = clamped * twoOverPi;
	int32 k = static_cast<int32>(scaled + (scaled < 0.0f ? -0.5f : 0.5f));
	float kFloat = static_cast<float>(k);

	float remainder = clamped - kFloat * halfPiHigh;
	remainder = remainder - kFloat * halfPiMiddle;
	remainder = remainder - kFloat * halfPiLow;

	return ReducedAngle{remainder, k & 3};
}


// sin(r) for |r| <= pi/4: the Taylor series to r^9. The omitted term is below 2e-9.
inline float SinPolynomial(float r) {
	float r2 = r * r;
	return r + r * r2 * (-1.6666667e-1f + r2 * (8.3333333e-3f + r2 * (-1.9841270e-4f + r2 * 2.7557319e-6f)));
}


// cos(r) for |r| <= pi/4: the Taylor series to r^10. The omitted term is below 1e-10.
inline float CosPolynomial(float r) {
	float r2 = r * r;
	return 1.0f + r2 * (-5.0e-1f + r2 * (4.1666668e-2f + r2 * (-1.3888889e-3f + r2 * (2.4801587e-5f + r2 * -2.7557319e-7f))));
}


// sin of the angle at a quadrant with the given remainder. The odd quadrants use the cosine of the remainder.
inline float SineAtQuadrant(float remainder, int32 quadrant) {
	float value = (quadrant & 1) != 0 ? CosPolynomial(remainder) : SinPolynomial(remainder);
	return (quadrant & 2) != 0 ? -value : value;
}

}


// Sine and cosine of a float argument in radians, without libm.
inline float FastSin(float x) {
	FastTrigDetail::ReducedAngle angle = FastTrigDetail::ReduceHalfPi(x);
	return FastTrigDetail::SineAtQuadrant(angle.remainder, angle.quadrant);
}

inline float FastCos(float x) {
	FastTrigDetail::ReducedAngle angle = FastTrigDetail::ReduceHalfPi(x);
	return FastTrigDetail::SineAtQuadrant(angle.remainder, (angle.quadrant + 1) & 3);
}
