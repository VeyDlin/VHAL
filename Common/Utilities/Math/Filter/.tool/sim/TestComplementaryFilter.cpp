#include <Utilities/Math/Filter/ComplementaryFilter.h>
#include <Utilities/Math/IQMath/IQMath.h>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>


// Host tests for ComplementaryFilter with float and IQ types. The reference is a double implementation of the same formulas
// and tuning as the class, written in this file, so the expected angles do not come from the class under test. It checks the
// fixed-point arithmetic against double; it does not check the algorithm, which the float output comparison covers.
//
// The scenario is a known attitude. Pitch swings by 30 degrees at 0.5 Hz and roll by 60 degrees at 0.3 Hz. The accelerometer
// reads the gravity direction of that attitude with a deterministic vibration of up to 0.02 g, and between samples 2000 and
// 2200 the accelerometer reads 1.5 times gravity, which turns the gravity correction off. The gyroscope reports the exact
// angular rates. Every type gets the same samples.


namespace {


int failureCount = 0;


// Bounds in degrees over the whole scenario.
//   float: single precision in every step, about 4000 steps of errors near 1e-6 degree.
//   IQ<16>: the correction dt / (1 s + dt), about 0.001, has 64 raw steps, and the atan2 approximation (about 0.005 rad
//           at small arguments, from input quantization) adds to the angle error.
//   IQ<20>: 16 times finer steps, so the atan2 approximation (0.0005 rad, 0.03 degree) is the limit.
constexpr double floatBoundDeg = 0.001;
constexpr double q16BoundDeg = 0.1;
constexpr double q20BoundDeg = 0.05;

// The sample time is 2^-10 s, which every IQ type with Q of 10 or more holds exactly. A 1 ms sample time is not exact at Q16:
// 0.001 rounds to 66 raw steps, 0.7 percent high, and the gyroscope integral scales with it (about 0.5 degree in 4 s).
constexpr uint32 sampleCount = 4000;
constexpr double dtSec = 1.0 / 1024.0;
constexpr double degPerRad = 180.0 / 3.14159265358979323846;
constexpr double twoPi = 2.0 * 3.14159265358979323846;


struct RawSample {
	double accelX = 0.0;
	double accelY = 0.0;
	double accelZ = 0.0;
	double gyroX = 0.0;
	double gyroY = 0.0;
	double gyroZ = 0.0;
};


struct Angles {
	double pitchDeg = 0.0;
	double rollDeg = 0.0;
};


// Double reference of the filter formulas, with the same tuning defaults: time constant 1 s, accelerometer corner 10 Hz,
// trust window 0.25 g.
class ReferenceFilter {
public:
	void Reset(const RawSample& sample) {
		double accelPitch = 0.0;
		double accelRoll = 0.0;
		AccelAngles(sample, accelPitch, accelRoll);
		pitchDeg = accelPitch;
		rollDeg = accelRoll;
		accelPitchLowDeg = accelPitch;
		accelRollLowDeg = accelRoll;
	}


	void Update(const RawSample& sample, double dt) {
		double accelPitch = 0.0;
		double accelRoll = 0.0;
		AccelAngles(sample, accelPitch, accelRoll);

		double lowAlpha = 1.0 - std::exp(-twoPi * 10.0 * dt);
		accelPitchLowDeg = accelPitchLowDeg + lowAlpha * (accelPitch - accelPitchLowDeg);
		accelRollLowDeg = accelRollLowDeg + lowAlpha * (accelRoll - accelRollLowDeg);

		double norm = std::sqrt(sample.accelX * sample.accelX + sample.accelY * sample.accelY + sample.accelZ * sample.accelZ);
		double trust = 1.0 - std::fabs(norm - 1.0) / 0.25;
		if (trust < 0.0) {
			trust = 0.0;
		}
		if (trust > 1.0) {
			trust = 1.0;
		}

		double correction = trust * dt / (1.0 + dt);
		pitchDeg = (pitchDeg + sample.gyroY * dt) * (1.0 - correction) + accelPitchLowDeg * correction;
		rollDeg = (rollDeg + sample.gyroX * dt) * (1.0 - correction) + accelRollLowDeg * correction;
	}


	Angles Get() const {
		Angles result;
		result.pitchDeg = pitchDeg;
		result.rollDeg = rollDeg;
		return result;
	}


private:
	static void AccelAngles(const RawSample& sample, double& pitch, double& roll) {
		double yz = std::sqrt(sample.accelY * sample.accelY + sample.accelZ * sample.accelZ);
		pitch = std::atan2(-sample.accelX, yz) * degPerRad;
		roll = std::atan2(sample.accelY, sample.accelZ) * degPerRad;
	}


	double pitchDeg = 0.0;
	double rollDeg = 0.0;
	double accelPitchLowDeg = 0.0;
	double accelRollLowDeg = 0.0;
};


void ExpectTrue(bool condition, int line) {
	if (!condition) {
		failureCount++;
		std::fprintf(stderr, "TestComplementaryFilter.cpp:%d: condition failed\n", line);
	}
}


// Builds the scenario: the attitude, the gravity it implies, a deterministic vibration and the exact rates.
void BuildScenario(std::array<RawSample, sampleCount>& samples, std::array<Angles, sampleCount>& truth) {
	uint32 state = 12345;
	for (uint32 index = 0; index < sampleCount; index++) {
		double t = index * dtSec;
		double pitchDeg = 30.0 * std::sin(twoPi * 0.5 * t);
		double rollDeg = 60.0 * std::sin(twoPi * 0.3 * t + 0.5);
		double pitchRateDeg = 30.0 * twoPi * 0.5 * std::cos(twoPi * 0.5 * t);
		double rollRateDeg = 60.0 * twoPi * 0.3 * std::cos(twoPi * 0.3 * t + 0.5);

		double pitch = pitchDeg / degPerRad;
		double roll = rollDeg / degPerRad;

		state = state * 1664525u + 1013904223u;
		double noiseX = (static_cast<double>(state >> 8) / 16777216.0 - 0.5) * 0.04;
		state = state * 1664525u + 1013904223u;
		double noiseY = (static_cast<double>(state >> 8) / 16777216.0 - 0.5) * 0.04;
		state = state * 1664525u + 1013904223u;
		double noiseZ = (static_cast<double>(state >> 8) / 16777216.0 - 0.5) * 0.04;

		RawSample& sample = samples[index];
		sample.accelX = -std::sin(pitch) + noiseX;
		sample.accelY = std::cos(pitch) * std::sin(roll) + noiseY;
		sample.accelZ = std::cos(pitch) * std::cos(roll) + noiseZ;
		if (index >= 2000 && index < 2200) {
			sample.accelX = sample.accelX * 1.5;
			sample.accelY = sample.accelY * 1.5;
			sample.accelZ = sample.accelZ * 1.5;
		}
		sample.gyroX = rollRateDeg;
		sample.gyroY = pitchRateDeg;
		sample.gyroZ = 0.0;

		truth[index].pitchDeg = pitchDeg;
		truth[index].rollDeg = rollDeg;
	}
}


// The value of an IQ or float as a double, for the error. An IQ is read through its raw value, so no rounding is added.
template<typename T>
double AsDouble(T value) {
	if constexpr (IQType<T>) {
		return static_cast<double>(value.raw) / static_cast<double>(value.one);
	} else {
		return static_cast<double>(value);
	}
}


// Runs the class under test with number type T over the samples and returns the largest angle error against the reference.
// The samples are double; an IQ takes them as floats, so each one is converted with static_cast<float> first.
template<typename T>
double RunType(const std::array<RawSample, sampleCount>& samples, const std::array<Angles, sampleCount>& reference) {
	ComplementaryFilter<T> filter;
	typename ComplementaryFilter<T>::Sample first;
	first.accelX = RealFrom<T>(static_cast<float>(samples[0].accelX));
	first.accelY = RealFrom<T>(static_cast<float>(samples[0].accelY));
	first.accelZ = RealFrom<T>(static_cast<float>(samples[0].accelZ));
	first.gyroX = RealFrom<T>(static_cast<float>(samples[0].gyroX));
	first.gyroY = RealFrom<T>(static_cast<float>(samples[0].gyroY));
	first.gyroZ = RealFrom<T>(static_cast<float>(samples[0].gyroZ));
	filter.Reset(first);

	double maxError = 0.0;
	for (uint32 index = 1; index < sampleCount; index++) {
		typename ComplementaryFilter<T>::Sample sample;
		sample.accelX = RealFrom<T>(static_cast<float>(samples[index].accelX));
		sample.accelY = RealFrom<T>(static_cast<float>(samples[index].accelY));
		sample.accelZ = RealFrom<T>(static_cast<float>(samples[index].accelZ));
		sample.gyroX = RealFrom<T>(static_cast<float>(samples[index].gyroX));
		sample.gyroY = RealFrom<T>(static_cast<float>(samples[index].gyroY));
		sample.gyroZ = RealFrom<T>(static_cast<float>(samples[index].gyroZ));
		filter.Update(sample, RealFrom<T>(static_cast<float>(dtSec)));

		double pitchError = std::fabs(AsDouble(filter.GetPitchDeg()) - reference[index].pitchDeg);
		double rollError = std::fabs(AsDouble(filter.GetRollDeg()) - reference[index].rollDeg);
		if (pitchError > maxError) {
			maxError = pitchError;
		}
		if (rollError > maxError) {
			maxError = rollError;
		}
	}
	return maxError;
}


void CheckType(const char* name, double boundDeg, double maxError, int line) {
	std::printf("%-10s max angle error %.6f deg, bound %.3f deg\n", name, maxError, boundDeg);
	if (maxError > boundDeg) {
		failureCount++;
		std::fprintf(stderr, "TestComplementaryFilter.cpp:%d: %s error %.6f exceeds %.3f\n", line, name, maxError, boundDeg);
	}
}


void RunReference(const std::array<RawSample, sampleCount>& samples, std::array<Angles, sampleCount>& reference) {
	ReferenceFilter filter;
	filter.Reset(samples[0]);
	reference[0] = filter.Get();
	for (uint32 index = 1; index < sampleCount; index++) {
		filter.Update(samples[index], dtSec);
		reference[index] = filter.Get();
	}
}


void RunAll() {
	std::array<RawSample, sampleCount> samples;
	std::array<Angles, sampleCount> truth;
	std::array<Angles, sampleCount> reference;
	BuildScenario(samples, truth);
	RunReference(samples, reference);

	// The reference must track the attitude, so the scenario is meaningful. The filter lags the accelerometer during the
	// shock window, so the check is loose.
	double referenceToTruth = 0.0;
	for (uint32 index = 500; index < sampleCount; index++) {
		double pitchGap = std::fabs(reference[index].pitchDeg - truth[index].pitchDeg);
		double rollGap = std::fabs(reference[index].rollDeg - truth[index].rollDeg);
		if (pitchGap > referenceToTruth) {
			referenceToTruth = pitchGap;
		}
		if (rollGap > referenceToTruth) {
			referenceToTruth = rollGap;
		}
	}
	std::printf("reference against the true attitude after 0.5 s: %.3f deg\n", referenceToTruth);
	ExpectTrue(referenceToTruth < 3.0, __LINE__);

	double floatError = RunType<float>(samples, reference);
	CheckType("float", floatBoundDeg, floatError, __LINE__);

	double q16Error = RunType<IQ<16>>(samples, reference);
	CheckType("IQ<16>", q16BoundDeg, q16Error, __LINE__);

	double q20Error = RunType<IQ<20>>(samples, reference);
	CheckType("IQ<20>", q20BoundDeg, q20Error, __LINE__);
}


}


int main() {
	RunAll();

	if (failureCount != 0) {
		std::fprintf(stderr, "%d check(s) failed\n", failureCount);
		return 1;
	}
	std::printf("TestComplementaryFilter passed\n");
	return 0;
}
