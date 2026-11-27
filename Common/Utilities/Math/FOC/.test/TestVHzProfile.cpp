#include "FOCCheck.h"
#include <Utilities/Math/FOC/VHzProfile.h>
#include <Utilities/Math/IQMath/IQ.h>
#include <cmath>
#include <cstdio>


// Host tests for VHzProfile with float, IQ<16> and IQ<24>. The profile is lowFreq 0.1, highFreq 0.5, voltMax 1.0,
// voltMin 0.1. Between lowFreq and highFreq the slope is (1.0 - 0.1) / (0.5 - 0.1) = 2.25, so the voltage is
// 0.1 + 2.25 * (f - 0.1). Above highFreq the voltage is voltMax. The expected values are worked out by hand in each case.


namespace {


int failureCount = 0;


void ExpectNear(double actual, double expected, const char* name, int line) {
	if (std::fabs(actual - expected) > 1e-4) {
		failureCount++;
		std::fprintf(stderr, "TestVHzProfile.cpp:%d: %s is %.6f, expected %.6f\n", line, name, actual, expected);
	}
}


template <typename T>
void Fill(VHzProfile<T>& profile) {
	profile.parameters = {
		.lowFreq = T(0.1),
		.highFreq = T(0.5),
		.voltMax = T(1.0),
		.voltMin = T(0.1)
	};
}


template <typename T>
double Resolve(VHzProfile<T>& profile, double frequency) {
	profile.Set(FOCCheck::FromDouble<T>(frequency)).Resolve();
	return FOCCheck::ToDouble(profile.Get().voltage);
}


template <typename T>
void Cases(const char* name) {
	VHzProfile<T> profile;
	Fill(profile);

	// Below lowFreq: the boost voltage 0.1.
	ExpectNear(Resolve(profile, 0.05), 0.1, name, __LINE__);

	// Between lowFreq and highFreq: 0.1 + 2.25 * (0.3 - 0.1) = 0.55.
	ExpectNear(Resolve(profile, 0.3), 0.55, name, __LINE__);

	// Negative frequency uses its magnitude: 0.1 + 2.25 * (0.3 - 0.1) = 0.55.
	ExpectNear(Resolve(profile, -0.3), 0.55, name, __LINE__);

	// At highFreq: 0.1 + 2.25 * (0.5 - 0.1) = 1.0.
	ExpectNear(Resolve(profile, 0.5), 1.0, name, __LINE__);

	// Above highFreq (0.55 is past the rated frequency): the rated voltage 1.0.
	ExpectNear(Resolve(profile, 0.55), 1.0, name, __LINE__);

	// Well above highFreq, the voltage must be the maximum voltage 1.0, not the 0.55 from the call above.
	Resolve(profile, 0.3);
	ExpectNear(Resolve(profile, 0.6), 1.0, name, __LINE__);

	// Above highFreq: the maximum voltage 1.0, also for a negative frequency.
	Resolve(profile, 0.3);
	ExpectNear(Resolve(profile, 0.9), 1.0, name, __LINE__);

	Resolve(profile, 0.3);
	ExpectNear(Resolve(profile, -0.6), 1.0, name, __LINE__);
}


void IQ24ExactCases() {
	using Q24 = IQ<24>;

	VHzProfile<Q24> profile;
	Fill(profile);

	// At 0.6 (above highFreq) the output is exactly the maximum voltage, raw 1.0 = 2^24.
	profile.Set(Q24(0.3)).Resolve();
	profile.Set(Q24(0.6)).Resolve();
	if (profile.Get().voltage.raw != Q24(1.0).raw) {
		failureCount++;
		std::fprintf(stderr, "TestVHzProfile.cpp:%d: IQ24 voltage at 0.6 has raw %d, expected %d\n", __LINE__,
			static_cast<int>(profile.Get().voltage.raw), static_cast<int>(Q24(1.0).raw));
	}

	// At 2.0 the output is the same exact maximum.
	profile.Set(Q24(0.3)).Resolve();
	profile.Set(Q24(2.0)).Resolve();
	if (profile.Get().voltage.raw != Q24(1.0).raw) {
		failureCount++;
		std::fprintf(stderr, "TestVHzProfile.cpp:%d: IQ24 voltage at 2.0 has raw %d, expected %d\n", __LINE__,
			static_cast<int>(profile.Get().voltage.raw), static_cast<int>(Q24(1.0).raw));
	}
}


}


int main() {
	Cases<float>("float");
	Cases<IQ<16>>("IQ16");
	Cases<IQ<24>>("IQ24");
	IQ24ExactCases();

	if (failureCount != 0) {
		std::fprintf(stderr, "%d check(s) failed\n", failureCount);
		return 1;
	}

	std::printf("all checks passed\n");
	return 0;
}
