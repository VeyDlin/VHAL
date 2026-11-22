#include "FOCCheck.h"
#include <Utilities/Math/FOC/CommutationTrigger.h>
#include <Utilities/Math/FOC/Impulse.h>
#include <Utilities/Math/FOC/RampController.h>
#include <Utilities/Math/IQMath/IQMath.h>
#include <cmath>
#include <cstdio>
#include <type_traits>


// Host checks for the trigger and ramp blocks. CommutationTrigger is run against its double instance on the same
// quantized inputs: its counters are integers, and the only T arithmetic is a sum and a product by 3, so IQ must match
// the double instance exactly. Impulse takes an integer period. RampController is run against its double instance, and
// its default resolution is checked against 2^-15.


namespace {

constexpr int steps = 3000;


template<typename T>
double Q(double value) {
	return FOCCheck::Quantized<T>(value);
}


// The triggers in a sequence of commutation states. A phase voltage with the given offset is a sine over 240 steps; the
// state changes every 40 steps.
template<typename T>
void CommutationChecks(const char* name) {
	typename CommutationTrigger<T>::WindowsNoise noise{.delta = 1, .dynamicThreshold = 6};
	typename CommutationTrigger<T>::Options options{.maxOutput = 0x0000FFFF};
	typename CommutationTrigger<double>::WindowsNoise noiseRef{.delta = 1, .dynamicThreshold = 6};
	typename CommutationTrigger<double>::Options optionsRef{.maxOutput = 0x0000FFFF};

	CommutationTrigger<T> trigger;
	CommutationTrigger<double> reference;
	trigger.SetWindowsNoise(noise).SetOptions(options);
	reference.SetWindowsNoise(noiseRef).SetOptions(optionsRef);

	int mismatches = 0;
	int triggers = 0;
	uint32 virtualTimer = 0;
	for (int k = 0; k < steps; k++) {
		// The offset of pi / 6 puts the zero crossings of the de-energized phases inside the commutation states.
		double angle = 2 * 3.14159265358979323846 * k / 240.0 + 0.52359877559829887;
		double amplitude = 0.3 + 0.9 * (0.5 + 0.5 * std::sin(angle / 7.0));
		double va = Q<T>(amplitude * std::sin(angle));
		double vb = Q<T>(amplitude * std::sin(angle - 2.0943951023931953));
		double vc = Q<T>(amplitude * std::sin(angle + 2.0943951023931953));
		virtualTimer++;

		typename CommutationTrigger<T>::Input input{
			.Va = T(va),
			.Vb = T(vb),
			.Vc = T(vc),
			.commutationPointer = static_cast<uint32>((k / 40) % 6),
			.virtualTimer = virtualTimer
		};
		typename CommutationTrigger<double>::Input inputRef{
			.Va = va,
			.Vb = vb,
			.Vc = vc,
			.commutationPointer = static_cast<uint32>((k / 40) % 6),
			.virtualTimer = virtualTimer
		};

		trigger.SetInput(input).Resolve();
		reference.SetInput(inputRef).Resolve();
		if (trigger.Get().trigger != reference.Get().trigger) {
			mismatches++;
		}
		if (trigger.Get().trigger) {
			triggers++;
		}
	}

	char label[96];
	std::snprintf(label, sizeof label, "CommutationTrigger matches the double instance %s", name);
	FOCCheck::Expect(mismatches == 0, label, __LINE__);
	std::snprintf(label, sizeof label, "CommutationTrigger fires %s", name);
	FOCCheck::Expect(triggers > 0, label, __LINE__);
}


void ImpulseChecks() {
	Impulse impulse(7);
	int count = 0;
	bool periodOk = true;
	for (int k = 1; k <= steps; k++) {
		impulse.Resolve();
		if (impulse.IsImpulse()) {
			count++;
			if (k % 7 != 0) {
				periodOk = false;
			}
		}
	}
	FOCCheck::Expect(periodOk && count == steps / 7, "Impulse period 7 fires every seventh call", __LINE__);

	impulse.SetPeriod(5);
	count = 0;
	for (int k = 1; k <= steps; k++) {
		impulse.Resolve();
		if (impulse.IsImpulse()) {
			count++;
		}
	}
	FOCCheck::Expect(count == steps / 5, "Impulse after SetPeriod(5) fires every fifth call", __LINE__);
}


template<typename T>
void RampChecks(const char* name, double bound) {
	RampController<T> ramp;
	RampController<double> reference;
	reference.parameters.resolution = Q<T>(1.0 / 32768.0);
	ramp.parameters.delayMax = 5;
	reference.parameters.delayMax = 5;
	ramp.SetTargetPosition(T(0.3));
	reference.SetTargetPosition(Q<T>(0.3));

	double maxError = 0;
	// The target 0.3 takes 0.3 / 2^-15 = 9830 steps of 5 calls each.
	for (int k = 0; k < 60000; k++) {
		ramp.Resolve();
		reference.Resolve();
		maxError = std::fmax(maxError, std::fabs(FOCCheck::ToDouble(ramp.Get().position) - reference.Get().position));
	}

	char label[96];
	std::snprintf(label, sizeof label, "RampController against the double instance %s", name);
	FOCCheck::Report(label, maxError, bound, __LINE__);
	std::snprintf(label, sizeof label, "RampController reaches the target %s", name);
	FOCCheck::Expect(ramp.Get().equalFlag && FOCCheck::ToDouble(ramp.Get().position) == Q<T>(0.3), label, __LINE__);
}


void DefaultResolutionChecks() {
	RampController<float> rampFloat;
	RampController<IQ<16>> rampIQ16;
	RampController<IQ<24>> rampIQ24;
	FOCCheck::Expect(rampFloat.parameters.resolution == float(1.0 / 32768.0), "RampController float default resolution is 2^-15", __LINE__);
	FOCCheck::Expect(rampIQ16.parameters.resolution.raw == 2, "RampController IQ16 default resolution is 2 LSB (2^-15)", __LINE__);
	FOCCheck::Expect(rampIQ24.parameters.resolution.raw == 512, "RampController IQ24 default resolution is 512 LSB (2^-15)", __LINE__);
}

}


// Impulse takes an integer period: a float period is rejected at compile time, so it cannot be truncated silently. The
// deleted float overloads are what these checks see.
static_assert(std::is_constructible_v<Impulse, uint32>, "Impulse takes uint32");
static_assert(std::is_constructible_v<Impulse, int>, "Impulse takes an int literal");
static_assert(!std::is_constructible_v<Impulse, float>, "Impulse rejects a float period");
static_assert(!std::is_constructible_v<Impulse, double>, "Impulse rejects a double period");
template<typename V, typename = void>
struct CanSetPeriod : std::false_type {
};


template<typename V>
struct CanSetPeriod<V, std::void_t<decltype(std::declval<Impulse&>().SetPeriod(std::declval<V>()))>> : std::true_type {
};


static_assert(CanSetPeriod<uint32>::value, "SetPeriod takes uint32");
static_assert(!CanSetPeriod<float>::value, "SetPeriod rejects a float");
static_assert(!CanSetPeriod<double>::value, "SetPeriod rejects a double");


int main() {
	CommutationChecks<float>("float");
	CommutationChecks<IQ<16>>("IQ16");
	CommutationChecks<IQ<20>>("IQ20");
	CommutationChecks<IQ<24>>("IQ24");

	ImpulseChecks();

	DefaultResolutionChecks();
	RampChecks<float>("float", 0.0);
	RampChecks<IQ<16>>("IQ16", 0.0);
	RampChecks<IQ<20>>("IQ20", 0.0);
	RampChecks<IQ<24>>("IQ24", 0.0);

	return FOCCheck::Finish();
}
