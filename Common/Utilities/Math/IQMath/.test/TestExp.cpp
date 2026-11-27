#include <Utilities/Math/IQMath/IQMath.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>


// Host tests for exp and expm1 in IQ. The reference is std::exp and std::expm1 in double, applied to the exact value of
// the input raw value. Each error is measured in LSB of the result format against the exact result, so it includes the
// final rounding. The bounds are the worst errors measured over every raw value of the sweep range, plus 0.25 LSB and
// rounded up to 0.01 (see the README of IQMath).


namespace {


int failureCount = 0;


void Fail(int line, const char* what, double value, double bound) {
	failureCount++;
	std::fprintf(stderr, "TestExp.cpp:%d: %s %.4f LSB, bound %.2f\n", line, what, value, bound);
}


template <typename RawType>
void ExpectRaw(RawType actual, int64_t expected, int line) {
	if (static_cast<int64_t>(actual) != expected) {
		failureCount++;
		std::fprintf(stderr, "TestExp.cpp:%d: raw %lld, expected %lld\n", line, static_cast<long long>(actual),
			static_cast<long long>(expected));
	}
}


// Worst error of exp and expm1 over the raw values whose results lie in the int32 range of the result, with the bounds for
// that region and for |x| below one half, where expm1 must stay accurate. Q up to 20 is swept at every raw value. Above
// that the sweep samples those Q with the steps given in main, to keep the test fast: a sampled run finds an error above a
// bound only if a sampled value reaches it. The bounds come from an exhaustive measurement of every raw value (see the
// README of IQMath), so a sampled Q is checked against a bound that was set on all values.
template<int Q, int L>
void Sweep(double fullBound, double smallBound, int64_t step, int line) {
	const double one = static_cast<double>(int64_t(1) << Q);
	const double ln2 = std::log(2.0);
	// exp leaves the int32 range beyond (31 - Q) ln 2, and expm1 beyond ln(2^(31 - Q) + 1), which is below (32 - Q) ln 2.
	// Below -(Q + 2) ln 2, exp is 0 and expm1 is -1, both within half an LSB of the reference.
	const double loX = -(Q + 2) * ln2;
	const double hiX = (32 - Q) * ln2;
	const int64_t lo = std::max<int64_t>(-(int64_t(1) << 31), static_cast<int64_t>(std::floor(loX * one)));
	const int64_t hi = std::min<int64_t>((int64_t(1) << 31) - 1, static_cast<int64_t>(std::ceil(hiX * one)));

	double worstExp = 0.0;
	double worstExpm1 = 0.0;
	double worstSmall = 0.0;

	for (int64_t raw = lo; raw <= hi; raw += step) {
		IQ<Q, L> x = IQ<Q, L>::FromRaw(static_cast<int32_t>(raw));
		double xv = static_cast<double>(x.raw) / static_cast<double>(x.one);

		double referenceExp = std::exp(xv) * one;
		if (referenceExp < 2147483000.0) {
			double errorExp = std::fabs(static_cast<double>(exp(x).raw) - referenceExp);
			if (errorExp > worstExp) {
				worstExp = errorExp;
			}
		}

		double referenceExpm1 = std::expm1(xv) * one;
		if (referenceExpm1 < 2147483000.0 && referenceExpm1 > -2147483000.0) {
			double errorExpm1 = std::fabs(static_cast<double>(expm1(x).raw) - referenceExpm1);
			if (errorExpm1 > worstExpm1) {
				worstExpm1 = errorExpm1;
			}
			if (std::fabs(xv) < 0.5 && errorExpm1 > worstSmall) {
				worstSmall = errorExpm1;
			}
		}
	}

	if (worstExp > fullBound) {
		Fail(line, "exp worst error", worstExp, fullBound);
	}
	if (worstExpm1 > fullBound) {
		Fail(line, "expm1 worst error", worstExpm1, fullBound);
	}
	if (worstSmall > smallBound) {
		Fail(line, "expm1 worst error for |x| < 0.5", worstSmall, smallBound);
	}
	std::printf("Q%d: exp %.4f LSB, expm1 %.4f LSB, expm1 for |x| < 0.5 %.4f LSB\n", Q, worstExp, worstExpm1, worstSmall);
}


// The results at zero, one and minus one, from the exact values of e.
void ExactCases() {
	// exp(0) = 1 and expm1(0) = 0 exactly.
	ExpectRaw(exp(IQ<16>(0)).raw, 65536, __LINE__);
	ExpectRaw(expm1(IQ<16>(0)).raw, 0, __LINE__);
	// exp(1) = 2.718281828 * 65536 = 178145.32, so 178145.
	ExpectRaw(exp(IQ<16>(1)).raw, 178145, __LINE__);
	// expm1(1) = (2.718281828 - 1) * 65536 = 112609.32, so 112609.
	ExpectRaw(expm1(IQ<16>(1)).raw, 112609, __LINE__);
	// exp(-1) = 0.3678794412 * 65536 = 24109.35, so 24109.
	ExpectRaw(exp(IQ<16>(-1)).raw, 24109, __LINE__);
	// The motivating case: 1 - exp(-x) for x = 66 LSB at Q16. x = -66 / 65536 = -0.00100708, and
	// 1 - exp(x) = -x - x^2 / 2 = 0.00100708 - 0.00000051 = 0.00100657, times 65536 is 65.96, so 66.
	ExpectRaw((IQ<16>(1) - exp(IQ<16>::FromRaw(-66))).raw, 66, __LINE__);
	// expm1 of the same argument: -0.00100657 * 65536 = -65.96, so -66.
	ExpectRaw(expm1(IQ<16>::FromRaw(-66)).raw, -66, __LINE__);
	// 1 - exp(-0.001) is within one LSB of the exact value at Q16. x = IQ<16>(-0.001) is -66 LSB (-65.536 rounds to -66),
	// so the expected raw value is 66 as above.
	ExpectRaw((IQ<16>(1) - exp(IQ<16>(-0.001))).raw, 66, __LINE__);
	// Q24: x = -16777 LSB (about -0.00099999). x + x^2 / 2 = -0.00099949428 (x = -0.00099999427), times 2^24 is -16768.73,
	// so expm1 is -16769.
	ExpectRaw(expm1(IQ<24>::FromRaw(-16777)).raw, -16769, __LINE__);
	// Saturation: exp(20) and expm1(16) are beyond the Q16 range (32767.99998), so they saturate to the maximum.
	ExpectRaw(exp(IQ<16>(20)).raw, 2147483647, __LINE__);
	ExpectRaw(expm1(IQ<16>(16)).raw, 2147483647, __LINE__);
	// Underflow: exp(-20) is 2e-9, far below half an LSB, so it is 0; expm1(-20) is -1 exactly at Q16 (raw -65536).
	ExpectRaw(exp(IQ<16>(-20)).raw, 0, __LINE__);
	ExpectRaw(expm1(IQ<16>(-20)).raw, -65536, __LINE__);
	// Q24 holds up to 128, so exp(5) = 148.41 saturates there too.
	ExpectRaw(exp(IQ<24>(5)).raw, 2147483647, __LINE__);
}


}


int main() {
	ExactCases();

	// Bounds per Q: the worst error over every raw value (README of IQMath) plus 0.25 LSB, rounded up to 0.01. The first
	// bound applies to exp and to expm1 over the whole sweep range, the second to expm1 for |x| below 0.5. Measured worst
	// errors, exp / expm1 / expm1 for |x| < 0.5, in LSB: Q8 1.1143 / 1.1143 / 0.4994, Q12 1.4561 / 1.4561 / 0.4998,
	// Q16 1.8067 / 1.8067 / 0.5000, Q20 2.1454 / 2.1454 / 0.5011, Q24 1.9197 / 1.9197 / 0.5210, Q28 1.7047 / 1.7047 / 0.8515,
	// Q30 1.5768 / 3.2815 / 1.5768.
	// Q8, Q12, Q16 and Q20 are swept at every raw value. Q24, Q28 and Q30 are sampled with steps 64, 512 and 640.
	Sweep<8, 256>(1.37, 0.75, 1, __LINE__);
	Sweep<12, 256>(1.71, 0.75, 1, __LINE__);
	Sweep<16, 256>(2.06, 0.76, 1, __LINE__);
	Sweep<20, 256>(2.40, 0.76, 1, __LINE__);
	Sweep<24, 256>(2.17, 0.78, 64, __LINE__);
	Sweep<28, 256>(1.96, 1.11, 512, __LINE__);
	Sweep<30, 256>(3.54, 1.83, 640, __LINE__);

	if (failureCount != 0) {
		std::fprintf(stderr, "TestExp: %d check(s) failed\n", failureCount);
		return 1;
	}
	std::printf("TestExp: all checks passed\n");
	return 0;
}
