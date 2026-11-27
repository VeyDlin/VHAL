#include <Utilities/Math/IQMath/IQMath.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>


// Host tests for sin and cos of IQ (radians), SinPhase and CosPhase (binary angles) and RadiansToPhase. The reference is
// std::sin and std::cos in double at the exact input: raw / 2^Q for IQ, 2 pi * phase / 2^32 for a binary angle. Each error
// is in LSB of the result format and includes the final rounding to Q bits.
//
// Sampling: Q16 is swept at every raw value in [-4 pi, 4 pi] (about 1.65 million values). Q20 and Q24 are sampled with the
// steps given in main over the same range (Q24 over [-4 pi, 4 pi] is also measured at every raw value, a one-off run listed
// in the README table of IQMath), and Q16 over the whole int32 range with a step of 1009. Quadrant boundaries (k pi / 2
// with k in [-8, 8], and two raw values either side) and the int32 extremes are checked at every Q listed. Phases are
// checked at every 2^k, 2^k - 1 and 2^k + 1, and on a dense sweep with a step of 65537.
//
// Each bound is the worst error of its sweep plus a margin of 0.02 LSB, rounded up to 0.01, except the Q24 [-4 pi, 4 pi]
// bound, which is the exhaustive one-off run plus the same margin. The spec asks for one LSB.


namespace {


int failureCount = 0;

constexpr double pi = 3.14159265358979323846;
constexpr double twoPi = 2.0 * pi;
constexpr double turnUnits = 4294967296.0;


void Fail(int line, const char* what, double value, double bound) {
	failureCount++;
	std::fprintf(stderr, "TestTrig.cpp:%d: %s %.4f LSB, bound %.2f\n", line, what, value, bound);
}


void ExpectRaw(int line, const char* what, int64_t actual, int64_t expected) {
	if (actual != expected) {
		failureCount++;
		std::fprintf(
			stderr,
			"TestTrig.cpp:%d: %s: raw %lld, expected %lld\n",
			line,
			what,
			static_cast<long long>(actual),
			static_cast<long long>(expected)
		);
	}
}


void Check(int line, const char* what, double worst, double bound) {
	std::printf("  %-44s worst %.4f LSB, bound %.2f\n", what, worst, bound);
	if (worst > bound) {
		Fail(line, what, worst, bound);
	}
}


// Bounds in LSB of the result format: the worst error measured in each sweep, plus a margin of 0.02 LSB, rounded up to
// 0.01. The identity is the spec's one LSB, which the sweeps reach exactly (see CheckIdentity).
constexpr double boundRadians16Range = 0.52;
constexpr double boundRadians16Int32 = 0.76;
constexpr double boundRadians20 = 0.53;
constexpr double boundRadians24 = 0.59;
constexpr double boundBoundary16 = 0.48;
constexpr double boundBoundary20 = 0.39;
constexpr double boundBoundary24 = 0.49;
constexpr double boundExtreme16 = 0.40;
constexpr double boundExtreme20 = 0.39;
constexpr double boundExtreme24 = 0.61;
constexpr double boundPhase16 = 0.52;
constexpr double boundPhase24 = 0.56;
constexpr double boundIdentity = 1.0;


// Radians in IQ<Q, L> over the raw range [rawLo, rawHi] with a step. Returns the worst error of sin and of cos.
template<int Q, int L>
void SweepRadians(int64_t rawLo, int64_t rawHi, int64_t step, double& worstSin, double& worstCos) {
	const double one = static_cast<double>(int64_t(1) << Q);
	for (int64_t raw = rawLo; raw <= rawHi; raw += step) {
		IQ<Q, L> x = IQ<Q, L>::FromRaw(static_cast<int32_t>(raw));
		const double exact = static_cast<double>(raw) / one;
		const double errorSin = std::fabs(static_cast<double>(::sin(x).raw) - std::sin(exact) * one);
		const double errorCos = std::fabs(static_cast<double>(::cos(x).raw) - std::cos(exact) * one);
		if (errorSin > worstSin) {
			worstSin = errorSin;
		}
		if (errorCos > worstCos) {
			worstCos = errorCos;
		}
	}
}


template<int Q, int L>
void CheckRadiansRange(int64_t rawLo, int64_t rawHi, int64_t step, const char* label, double bound, int line) {
	double worstSin = 0.0;
	double worstCos = 0.0;
	SweepRadians<Q, L>(rawLo, rawHi, step, worstSin, worstCos);
	char sinLabel[96];
	char cosLabel[96];
	std::snprintf(sinLabel, sizeof(sinLabel), "sin, %s", label);
	std::snprintf(cosLabel, sizeof(cosLabel), "cos, %s", label);
	Check(line, sinLabel, worstSin, bound);
	Check(line, cosLabel, worstCos, bound);
}


// Every quadrant boundary k pi / 2 in [-4 pi, 4 pi] (k in [-8, 8]) with two raw values either side.
template<int Q, int L>
void CheckBoundaries(double bound, int line) {
	const double one = static_cast<double>(int64_t(1) << Q);
	double worstSin = 0.0;
	double worstCos = 0.0;
	for (int64_t k = -8; k <= 8; k++) {
		const int64_t center = static_cast<int64_t>(std::llround(static_cast<double>(k) * pi / 2.0 * one));
		for (int64_t offset = -2; offset <= 2; offset++) {
			SweepRadians<Q, L>(center + offset, center + offset, 1, worstSin, worstCos);
		}
	}
	char sinLabel[96];
	char cosLabel[96];
	std::snprintf(sinLabel, sizeof(sinLabel), "sin, Q%d quadrant boundaries", Q);
	std::snprintf(cosLabel, sizeof(cosLabel), "cos, Q%d quadrant boundaries", Q);
	Check(line, sinLabel, worstSin, bound);
	Check(line, cosLabel, worstCos, bound);
}


// The int32 extremes and their neighbours, which are the largest arguments (128 rad at Q24, 32768 rad at Q16).
template<int Q, int L>
void CheckExtremes(double bound, int line) {
	const int64_t maxRaw = std::numeric_limits<int32_t>::max();
	const int64_t minRaw = std::numeric_limits<int32_t>::min();
	double worstSin = 0.0;
	double worstCos = 0.0;
	const int64_t raws[] = {maxRaw, maxRaw - 1, maxRaw - 2, minRaw, minRaw + 1, minRaw + 2, 0, 1, -1};
	for (int64_t raw : raws) {
		SweepRadians<Q, L>(raw, raw, 1, worstSin, worstCos);
	}
	char sinLabel[96];
	char cosLabel[96];
	std::snprintf(sinLabel, sizeof(sinLabel), "sin, Q%d int32 extremes", Q);
	std::snprintf(cosLabel, sizeof(cosLabel), "cos, Q%d int32 extremes", Q);
	Check(line, sinLabel, worstSin, bound);
	Check(line, cosLabel, worstCos, bound);
}


// SinPhase and CosPhase against the exact sine of the binary angle.
template<int Q>
void SweepPhase(uint32_t phase, double& worstSin, double& worstCos) {
	const double one = static_cast<double>(int64_t(1) << Q);
	const double angle = twoPi * static_cast<double>(phase) / turnUnits;
	const double errorSin = std::fabs(static_cast<double>(SinPhase<Q>(phase).raw) - std::sin(angle) * one);
	const double errorCos = std::fabs(static_cast<double>(CosPhase<Q>(phase).raw) - std::cos(angle) * one);
	if (errorSin > worstSin) {
		worstSin = errorSin;
	}
	if (errorCos > worstCos) {
		worstCos = errorCos;
	}
}


template<int Q>
void CheckPhases(double bound, int line) {
	double worstSin = 0.0;
	double worstCos = 0.0;

	// Powers of two and their neighbours, with the phases 0 and 2^32 - 1
	SweepPhase<Q>(0u, worstSin, worstCos);
	SweepPhase<Q>(0xFFFFFFFFu, worstSin, worstCos);
	for (uint32_t k = 0; k < 32; k++) {
		const uint32_t power = static_cast<uint32_t>(1u) << k;
		SweepPhase<Q>(power, worstSin, worstCos);
		SweepPhase<Q>(power - 1u, worstSin, worstCos);
		SweepPhase<Q>(power + 1u, worstSin, worstCos);
	}

	// Dense sweep over the whole turn
	for (uint64_t phase = 0; phase < 4294967296ull; phase += 65537) {
		SweepPhase<Q>(static_cast<uint32_t>(phase), worstSin, worstCos);
	}

	char sinLabel[96];
	char cosLabel[96];
	std::snprintf(sinLabel, sizeof(sinLabel), "SinPhase<%d>, powers of two and dense", Q);
	std::snprintf(cosLabel, sizeof(cosLabel), "CosPhase<%d>, powers of two and dense", Q);
	Check(line, sinLabel, worstSin, bound);
	Check(line, cosLabel, worstCos, bound);
}


// cos(x) and sin(x + pi / 2) differ by at most one LSB. The sum is formed in IQ, so the constant pi / 2 is rounded to Q,
// which moves the angle by up to half an LSB; that and the rounding of both results can add up to exactly one LSB.
template<int Q, int L>
void CheckIdentity(int64_t step, double bound, int line) {
	const double one = static_cast<double>(int64_t(1) << Q);
	const IQ<Q, L> halfPi(pi / 2.0);
	double worst = 0.0;
	for (int64_t raw = -static_cast<int64_t>(4.0 * pi * one); raw <= static_cast<int64_t>(4.0 * pi * one); raw += step) {
		IQ<Q, L> x = IQ<Q, L>::FromRaw(static_cast<int32_t>(raw));
		const double difference = std::fabs(static_cast<double>(::cos(x).raw) - static_cast<double>(::sin(x + halfPi).raw));
		if (difference > worst) {
			worst = difference;
		}
	}
	char label[96];
	std::snprintf(label, sizeof(label), "cos(x) against sin(x + pi/2), Q%d", Q);
	Check(line, label, worst, bound);
}


// Exact values that the spec and the hand calculation give directly.
void CheckExactValues() {
	ExpectRaw(__LINE__, "SinPhase<24>(0)", SinPhase<24>(0u).raw, 0);
	ExpectRaw(__LINE__, "SinPhase<24>(pi/2)", SinPhase<24>(0x40000000u).raw, int64_t(1) << 24);
	ExpectRaw(__LINE__, "SinPhase<24>(pi)", SinPhase<24>(0x80000000u).raw, 0);
	ExpectRaw(__LINE__, "SinPhase<24>(3pi/2)", SinPhase<24>(0xC0000000u).raw, -(int64_t(1) << 24));
	ExpectRaw(__LINE__, "CosPhase<24>(0)", CosPhase<24>(0u).raw, int64_t(1) << 24);
	ExpectRaw(__LINE__, "CosPhase<24>(pi)", CosPhase<24>(0x80000000u).raw, -(int64_t(1) << 24));
	ExpectRaw(__LINE__, "sin(iq24 pi/2)", ::sin(IQ<24>(pi / 2.0)).raw, int64_t(1) << 24);
	ExpectRaw(__LINE__, "cos(iq24 0)", ::cos(IQ<24>(0)).raw, int64_t(1) << 24);
	ExpectRaw(__LINE__, "sin(iq16 0)", ::sin(IQ<16>(0)).raw, 0);

	// Radians as a phase: 1 rad is 2^32 / (2 pi) = 683565275.58 units, which rounds to 683565276
	static_assert(RadiansToPhase(0.0) == 0u);
	static_assert(RadiansToPhase(1.0) == 683565276u);
	static_assert(RadiansToPhase(pi / 2.0) == 0x40000000u);
	static_assert(RadiansToPhase(-pi / 2.0) == 0xC0000000u);
	static_assert(RadiansToPhase(pi) == 0x80000000u);
	static_assert(RadiansToPhase(twoPi) == 0u);
	static_assert(RadiansToPhase(3.0 * pi / 2.0) == 0xC0000000u);
}


// RadiansToPhase for arguments far beyond one turn. The reference is long double at the same double argument, and the
// tolerance is the error of the double division by 2 pi: a relative 2^-52 of the turns, in units. Above 1e15 rad the
// tolerance is no longer below half a turn, so the sizes stop there. Beyond 2^52 turns a double has no fraction, so the
// phase is 0, and NaN and infinity give 0 too.
void CheckLargeRadians() {
	const long double twoPiLong = 6.283185307179586476925286766559L;
	const long double turnUnitsLong = 4294967296.0L;
	const double arguments[] = {1.0e9, -1.0e9, 1.0e12, -1.0e12, 1.0e15, -1.0e15};
	for (double radians : arguments) {
		const long double turns = static_cast<long double>(radians) / twoPiLong;
		const long double fraction = turns - std::floor(turns);
		const long double expected = fraction * turnUnitsLong;
		const long double actual = static_cast<long double>(RadiansToPhase(radians));
		long double delta = actual - expected;
		delta -= turnUnitsLong * std::round(delta / turnUnitsLong);
		const long double tolerance = std::fabs(turns) * 2.22e-16L * turnUnitsLong + 1.0L;
		if (std::fabs(delta) > tolerance) {
			Fail(
				__LINE__,
				"RadiansToPhase far beyond one turn (units)",
				static_cast<double>(delta),
				static_cast<double>(tolerance)
			);
		}
	}

	const double infinity = std::numeric_limits<double>::infinity();
	const double notANumber = std::numeric_limits<double>::quiet_NaN();
	const double zeroArguments[] = {1.0e300, -1.0e300, infinity, -infinity, notANumber};
	for (double radians : zeroArguments) {
		const uint32_t phase = RadiansToPhase(radians);
		if (phase != 0u) {
			Fail(
				__LINE__,
				"RadiansToPhase beyond 2^52 turns or non-finite is not 0",
				static_cast<double>(phase),
				0.0
			);
		}
	}
}


} // namespace


int main() {
	// The Q16 full sweep covers every raw in [-4 pi, 4 pi]; the wider ranges and other Q are sampled with the steps given.
	const int64_t limit16 = static_cast<int64_t>(std::llround(4.0 * pi * 65536.0));
	const int64_t limit20 = static_cast<int64_t>(std::llround(4.0 * pi * 1048576.0));
	const int64_t limit24 = static_cast<int64_t>(std::llround(4.0 * pi * 16777216.0));

	std::printf("Radians in IQ, Q16, every raw value in [-4 pi, 4 pi]\n");
	CheckRadiansRange<16, 256>(-limit16, limit16, 1, "Q16 every raw in [-4 pi, 4 pi]", boundRadians16Range, __LINE__);

	std::printf("Radians in IQ, Q16, int32 range, step 1009 (sampled)\n");
	CheckRadiansRange<16, 256>(
		std::numeric_limits<int32_t>::min(),
		std::numeric_limits<int32_t>::max(),
		1009,
		"Q16 int32 range, step 1009",
		boundRadians16Int32,
		__LINE__
	);

	std::printf("Radians in IQ, Q20, step 7 over [-4 pi, 4 pi] (sampled)\n");
	CheckRadiansRange<20, 256>(-limit20, limit20, 7, "Q20 step 7 over [-4 pi, 4 pi]", boundRadians20, __LINE__);

	std::printf("Radians in IQ, Q24, step 97 over [-4 pi, 4 pi] (sampled)\n");
	CheckRadiansRange<24, 256>(-limit24, limit24, 97, "Q24 step 97 over [-4 pi, 4 pi]", boundRadians24, __LINE__);

	std::printf("Quadrant boundaries and int32 extremes\n");
	CheckBoundaries<16, 256>(boundBoundary16, __LINE__);
	CheckBoundaries<20, 256>(boundBoundary20, __LINE__);
	CheckBoundaries<24, 256>(boundBoundary24, __LINE__);
	CheckExtremes<16, 256>(boundExtreme16, __LINE__);
	CheckExtremes<20, 256>(boundExtreme20, __LINE__);
	CheckExtremes<24, 256>(boundExtreme24, __LINE__);

	std::printf("Binary angles\n");
	CheckPhases<16>(boundPhase16, __LINE__);
	CheckPhases<24>(boundPhase24, __LINE__);

	std::printf("Identity cos(x) = sin(x + pi/2)\n");
	CheckIdentity<16, 256>(1, boundIdentity, __LINE__);
	CheckIdentity<24, 256>(97, boundIdentity, __LINE__);

	std::printf("Exact values\n");
	CheckExactValues();

	std::printf("RadiansToPhase far beyond one turn\n");
	CheckLargeRadians();

	if (failureCount != 0) {
		std::fprintf(stderr, "TestTrig: %d failure(s)\n", failureCount);
		return 1;
	}
	std::printf("TestTrig: all checks passed\n");
	return 0;
}
