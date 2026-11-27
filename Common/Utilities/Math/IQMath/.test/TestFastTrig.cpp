#include <Utilities/Math/IQMath/FastTrig.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>


// Host tests for FastSin and FastCos, the float sine and cosine without libm. The reference is std::sin and std::cos in
// double at the float argument, so the error includes the rounding of the result to float and the error of the argument
// reduction. Sampling: every float argument reached by the steps in main over [-1e4, 1e4] (step 1e-3 in double, rounded
// to float), the multiples of pi/2 with k in [-6400, 6400] (up to about 1.0053e4), a fine sweep near zero, and the domain
// edge [1e4, 2.5e4] where the reduction is still exact. Arguments beyond the domain are checked for range and for the
// result of NaN and infinity, not for accuracy.
//
// The bounds are the worst errors measured over these samples, plus a margin of 0.02e-7, rounded up to 0.01e-7.


namespace {


int failureCount = 0;

constexpr double pi = 3.14159265358979323846;


void Fail(int line, const char* what, double value, double bound) {
	failureCount++;
	std::fprintf(stderr, "TestFastTrig.cpp:%d: %s %.3e, bound %.2e\n", line, what, value, bound);
}


// The worst error of FastSin and FastCos over an argument range with a step.
void Sweep(double lo, double hi, double step, double& worstSin, double& worstCos, float& atSin, float& atCos) {
	for (double value = lo; value <= hi; value += step) {
		float x = static_cast<float>(value);
		double reference = static_cast<double>(x);
		double errorSin = std::fabs(static_cast<double>(FastSin(x)) - std::sin(reference));
		double errorCos = std::fabs(static_cast<double>(FastCos(x)) - std::cos(reference));
		if (errorSin > worstSin) {
			worstSin = errorSin;
			atSin = x;
		}
		if (errorCos > worstCos) {
			worstCos = errorCos;
			atCos = x;
		}
	}
}


void Report(int line, const char* label, double worstSin, float atSin, double worstCos, float atCos, double bound) {
	std::printf(
		"  %-40s sin %.3e at %.7g, cos %.3e at %.7g, bound %.2e\n",
		label,
		worstSin,
		static_cast<double>(atSin),
		worstCos,
		static_cast<double>(atCos),
		bound
	);
	if (worstSin > bound) {
		Fail(line, "sin worst error", worstSin, bound);
	}
	if (worstCos > bound) {
		Fail(line, "cos worst error", worstCos, bound);
	}
}


// Arguments beyond the domain are not accurate, but the result must still be finite and in [-1, 1]. NaN and infinity
// have no angle, so both functions give NaN for them.
void CheckOutsideDomain(int line) {
	const float finiteArguments[] = {
		3.0e9f,
		-3.0e9f,
		1.0e7f,
		-1.0e7f,
		1.6e6f,
		-1.6e6f,
		1.0e30f,
		-1.0e30f,
		std::numeric_limits<float>::max(),
		-std::numeric_limits<float>::max()
	};
	for (float x : finiteArguments) {
		float s = FastSin(x);
		float c = FastCos(x);
		if (!std::isfinite(s) || std::fabs(s) > 1.0f) {
			Fail(line, "FastSin outside the domain not in [-1, 1]", static_cast<double>(s), 1.0);
		}
		if (!std::isfinite(c) || std::fabs(c) > 1.0f) {
			Fail(line, "FastCos outside the domain not in [-1, 1]", static_cast<double>(c), 1.0);
		}
	}

	const float infinity = std::numeric_limits<float>::infinity();
	const float notANumber = std::numeric_limits<float>::quiet_NaN();
	const float nonFinite[] = {infinity, -infinity, notANumber};
	for (float x : nonFinite) {
		if (!std::isnan(FastSin(x))) {
			Fail(line, "FastSin of a non-finite argument is not NaN", static_cast<double>(FastSin(x)), 0.0);
		}
		if (!std::isnan(FastCos(x))) {
			Fail(line, "FastCos of a non-finite argument is not NaN", static_cast<double>(FastCos(x)), 0.0);
		}
	}
}


// Bounds in absolute error (the results are at most 1 in magnitude). Each is the measured worst of its sweep (8.52e-8 on
// [-1e4, 1e4], 8.55e-8 on [-8, 8], 2.98e-8 at multiples of pi/2, 8.52e-8 on [1e4, 2.5e4]) plus 0.02e-7, rounded up to
// 0.01e-7.
constexpr double boundWide = 8.8e-8;
constexpr double boundNearZero = 8.8e-8;
constexpr double boundMultiples = 3.2e-8;


} // namespace


int main() {
	std::printf("FastSin and FastCos against double, absolute error\n");

	double worstSin = 0.0;
	double worstCos = 0.0;
	float atSin = 0.0f;
	float atCos = 0.0f;
	Sweep(-1.0e4, 1.0e4, 1.0e-3, worstSin, worstCos, atSin, atCos);
	Report(__LINE__, "[-1e4, 1e4] step 1e-3", worstSin, atSin, worstCos, atCos, boundWide);

	worstSin = 0.0;
	worstCos = 0.0;
	Sweep(1.0e4, 2.5e4, 1.0e-2, worstSin, worstCos, atSin, atCos);
	Report(__LINE__, "[1e4, 2.5e4] step 1e-2 (domain edge)", worstSin, atSin, worstCos, atCos, boundWide);

	worstSin = 0.0;
	worstCos = 0.0;
	Sweep(-8.0, 8.0, 1.0e-6, worstSin, worstCos, atSin, atCos);
	Report(__LINE__, "[-8, 8] step 1e-6", worstSin, atSin, worstCos, atCos, boundNearZero);

	worstSin = 0.0;
	worstCos = 0.0;
	for (int64_t k = -6400; k <= 6400; k++) {
		float x = static_cast<float>(static_cast<double>(k) * pi / 2.0);
		double reference = static_cast<double>(x);
		double errorSin = std::fabs(static_cast<double>(FastSin(x)) - std::sin(reference));
		double errorCos = std::fabs(static_cast<double>(FastCos(x)) - std::cos(reference));
		if (errorSin > worstSin) {
			worstSin = errorSin;
			atSin = x;
		}
		if (errorCos > worstCos) {
			worstCos = errorCos;
			atCos = x;
		}
	}
	Report(__LINE__, "multiples of pi/2 in [-1e4, 1e4]", worstSin, atSin, worstCos, atCos, boundMultiples);

	// Exact values that do not depend on rounding: sin(0) = 0, cos(0) = 1, sin(pi/2) = 1, cos(pi) = -1
	if (FastSin(0.0f) != 0.0f || FastCos(0.0f) != 1.0f) {
		Fail(__LINE__, "sin(0) or cos(0)", 1.0, 0.0);
	}
	if (std::fabs(static_cast<double>(FastSin(static_cast<float>(pi / 2.0))) - 1.0) > 1.0e-7) {
		Fail(__LINE__, "sin(pi/2)", 1.0, 1.0e-7);
	}
	if (std::fabs(static_cast<double>(FastCos(static_cast<float>(pi))) + 1.0) > 1.0e-7) {
		Fail(__LINE__, "cos(pi)", 1.0, 1.0e-7);
	}

	std::printf("Arguments outside the domain: range and non-finite input\n");
	CheckOutsideDomain(__LINE__);

	if (failureCount != 0) {
		std::fprintf(stderr, "TestFastTrig: %d failure(s)\n", failureCount);
		return 1;
	}
	std::printf("TestFastTrig: all checks passed\n");
	return 0;
}
