#include <Utilities/Math/IQMath/IQ.h>
#include <Utilities/Math/MathUtilities.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>


// Host tests for MathUtilities::Interpolation and Extrapolation with float and IQ types. The expected values are worked out
// by hand in each case. The IQ cases compare raw values, so a rounding change shows up as an exact difference.


namespace {


int failureCount = 0;


void ExpectNear(double actual, double expected, int line) {
	if (std::fabs(actual - expected) > 1e-4) {
		failureCount++;
		std::fprintf(stderr, "TestInterpolation.cpp:%d: value %.6f, expected %.6f\n", line, actual, expected);
	}
}


template <typename RawType>
void ExpectRaw(RawType actual, int64_t expected, int line) {
	if (static_cast<int64_t>(actual) != expected) {
		failureCount++;
		std::fprintf(stderr, "TestInterpolation.cpp:%d: raw %lld, expected %lld\n", line, static_cast<long long>(actual),
			static_cast<long long>(expected));
	}
}


void FloatCases() {
	// Doc example: 10 + (2 - 0) * (18 - 10) / (4 - 0) = 14. Extrapolated to 6: 10 + 6 * 8 / 4 = 22.
	ExpectNear(MathUtilities::Interpolation<float>(0.0f, 10.0f, 4.0f, 18.0f, 2.0f), 14.0, __LINE__);
	ExpectNear(MathUtilities::Extrapolation<float>(0.0f, 10.0f, 4.0f, 18.0f, 6.0f), 22.0, __LINE__);

	// x2 equal to x1 gives y1: 10 + 4 * 8 / 4 = 18.
	ExpectNear(MathUtilities::Interpolation<float>(0.0f, 10.0f, 4.0f, 18.0f, 4.0f), 18.0, __LINE__);

	// Battery: 10 + (3.65 - 3.6) * (25 - 10) / (3.7 - 3.6) = 10 + 0.05 * 15 / 0.1 = 17.5.
	ExpectNear(MathUtilities::Interpolation<float>(3.6f, 10.0f, 3.7f, 25.0f, 3.65f), 17.5, __LINE__);

	// Survey cases: 90, 110 and 100, as the exact results.
	ExpectNear(MathUtilities::Interpolation<float>(0.0f, 0.0f, 1000.0f, 100.0f, 900.0f), 90.0, __LINE__);
	ExpectNear(MathUtilities::Extrapolation<float>(0.0f, 0.0f, 1000.0f, 100.0f, 1100.0f), 110.0, __LINE__);
	ExpectNear(MathUtilities::Interpolation<float>(10.0f, 0.0f, 100.0f, 200.0f, 55.0f), 100.0, __LINE__);
}


void IQ16Cases() {
	using Q16 = IQ<16>;

	// 0 + (900 - 0) * (100 - 0) / (1000 - 0) = 90. Raw: 90 * 65536 = 5898240.
	ExpectRaw(MathUtilities::Interpolation<Q16>(Q16(0), Q16(0), Q16(1000), Q16(100), Q16(900)).raw, 5898240, __LINE__);

	// Extrapolation: 0 + (1100 - 0) * (100 - 0) / (1000 - 0) = 110. Raw: 110 * 65536 = 7208960.
	ExpectRaw(MathUtilities::Extrapolation<Q16>(Q16(0), Q16(0), Q16(1000), Q16(100), Q16(1100)).raw, 7208960, __LINE__);

	// 0 + (55 - 10) * (200 - 0) / (100 - 10) = 100. Raw: 100 * 65536 = 6553600.
	ExpectRaw(MathUtilities::Interpolation<Q16>(Q16(10), Q16(0), Q16(100), Q16(200), Q16(55)).raw, 6553600, __LINE__);
	ExpectRaw(MathUtilities::Extrapolation<Q16>(Q16(10), Q16(0), Q16(100), Q16(200), Q16(55)).raw, 6553600, __LINE__);

	// Battery: the raw inputs are 3.6 -> 235929, 3.7 -> 242483, 3.65 -> 239206, and the outputs are 10 -> 655360, 25 -> 1638400.
	// dx = 3277, dd = 6554, dy = 983040, so dx * dy / dd = 491520 exactly. The result is 655360 + 491520 = 1146880 = 17.5.
	ExpectRaw(MathUtilities::Interpolation<Q16>(Q16(3.6), Q16(10), Q16(3.7), Q16(25), Q16(3.65)).raw, 1146880, __LINE__);

	// 1 / 3 = 21845.33 in raw units, so the result is 21845. The remainder 65536 is less than half of 196608.
	ExpectRaw(MathUtilities::Interpolation<Q16>(Q16::FromRaw(0), Q16::FromRaw(0), Q16::FromRaw(196608),
		Q16::FromRaw(65536), Q16::FromRaw(65536)).raw, 21845, __LINE__);

	// 2 / 3 = 43690.67 in raw units, so the result rounds up to 43691. The remainder 131072 is more than half of 196608.
	ExpectRaw(MathUtilities::Interpolation<Q16>(Q16::FromRaw(0), Q16::FromRaw(0), Q16::FromRaw(196608),
		Q16::FromRaw(65536), Q16::FromRaw(131072)).raw, 43691, __LINE__);

	// The same -2 / 3 with a negative y1 gives -43691: ties and rounding are symmetric around zero.
	ExpectRaw(MathUtilities::Interpolation<Q16>(Q16::FromRaw(0), Q16::FromRaw(0), Q16::FromRaw(196608),
		Q16::FromRaw(-65536), Q16::FromRaw(131072)).raw, -43691, __LINE__);

	// 60000 is above the IQ16 range (about 32768), so the result saturates to the largest raw value.
	ExpectRaw(MathUtilities::Extrapolation<Q16>(Q16(0), Q16(0), Q16(1), Q16(30000), Q16(2)).raw,
		std::numeric_limits<int32_t>::max(), __LINE__);
}


void IQ16ZeroDivisorCases() {
	using Q16 = IQ<16>;

	// x1 == x0 with x2 == x0: the difference product is 0, so the result is y0 = 7.
	ExpectRaw(MathUtilities::Interpolation<Q16>(Q16(5), Q16(7), Q16(5), Q16(9), Q16(5)).raw, 7 * 65536, __LINE__);

	// x1 == x0 with a positive product: the quotient saturates upward, as in the integer path.
	ExpectRaw(MathUtilities::Extrapolation<Q16>(Q16(5), Q16(7), Q16(5), Q16(9), Q16(6)).raw,
		std::numeric_limits<int32_t>::max(), __LINE__);

	// x1 == x0 with a negative product (x2 below x0): the quotient saturates downward.
	ExpectRaw(MathUtilities::Extrapolation<Q16>(Q16(5), Q16(7), Q16(5), Q16(9), Q16(4)).raw,
		std::numeric_limits<int32_t>::min(), __LINE__);
}


void IQ24Cases() {
	using Q24 = IQ<24>;

	// 0 + (90 - 0) * (10 - 0) / (100 - 0) = 9. Raw: 9 * 2^24 = 150994944.
	ExpectRaw(MathUtilities::Interpolation<Q24>(Q24(0), Q24(0), Q24(100), Q24(10), Q24(90)).raw, 150994944, __LINE__);

	// 0 + (110 - 0) * (10 - 0) / (100 - 0) = 11. Raw: 11 * 2^24 = 184549376.
	ExpectRaw(MathUtilities::Extrapolation<Q24>(Q24(0), Q24(0), Q24(100), Q24(10), Q24(110)).raw, 184549376, __LINE__);

	// 1 / 3 at Q24: 2^24 / 3 = 5592405.33 in raw units, so the result is 5592405.
	ExpectRaw(MathUtilities::Interpolation<Q24>(Q24::FromRaw(0), Q24::FromRaw(0), Q24::FromRaw(3 * 16777216),
		Q24::FromRaw(16777216), Q24::FromRaw(16777216)).raw, 5592405, __LINE__);
}


}


int main() {
	FloatCases();
	IQ16Cases();
	IQ16ZeroDivisorCases();
	IQ24Cases();

	if (failureCount != 0) {
		std::fprintf(stderr, "%d check(s) failed\n", failureCount);
		return 1;
	}

	std::printf("all checks passed\n");
	return 0;
}
