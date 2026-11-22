#include "FOCCheck.h"
#include <Utilities/Math/FOC/SpeedCalculator.h>
#include <Utilities/Math/FOC/SpeedPeriodCalculator.h>
#include <Utilities/Math/IQMath/IQMath.h>
#include <cmath>
#include <cstdio>
#include <limits>
#include <type_traits>


// Host checks for the speed blocks. SpeedCalculator is run against the double instance of the same template on the same
// quantized inputs, and its rpm against the 64-bit product, which must not saturate at Q24. SpeedPeriodCalculator takes
// the timer counts as integers up to 0x7FFF, so only Q16 and float are run.


namespace {

constexpr double ts = 1e-4;
constexpr int steps = 3000;
// The settled window holds a whole number of turns: 1600 steps are 4 turns at 25 Hz (400 steps per turn) and 8 at 50 Hz.
constexpr int settleStart = 1200;
constexpr int settleEnd = 2800;


template<typename T>
double Q(double value) {
	return FOCCheck::Quantized<T>(value);
}


// One LSB of T at 1 (the float epsilon for float).
template<typename T>
double Lsb() {
	if constexpr (std::is_floating_point_v<T>) {
		return static_cast<double>(std::numeric_limits<T>::epsilon());
	} else {
		return 1.0 / static_cast<double>(int64(1) << T::q);
	}
}


// K1 is 1 / (fb Ts), computed in SetupReal<T> and converted once: the error is the setup precision (float for IQ) and the
// rounding of K1 to T.
template<typename T>
void ComputeK1Check(const char* name, double baseFrequency, double bound) {
	T k1 = SpeedCalculator<T>::ComputeK1(baseFrequency, ts);
	double exact = 1.0 / (baseFrequency * ts);
	char label[96];
	std::snprintf(label, sizeof label, "SpeedCalculator ComputeK1 %s", name);
	FOCCheck::Report(label, std::fabs(FOCCheck::ToDouble(k1) - exact), bound, __LINE__);
}


// The angle rotates at rateHz from a start that is off the 0.1 and 0.9 window edges, so the speed is rateHz / fb in pu.
template<typename T>
void SpeedChecks(const char* name, double bound, double baseFrequency, double rateHz, uint32 baseRpm) {
	SpeedCalculator<T> calc;
	SpeedCalculator<double> reference;
	calc.baseSpeedRpm = baseRpm;
	reference.baseSpeedRpm = baseRpm;
	calc.K1 = SpeedCalculator<T>::ComputeK1(baseFrequency, ts);
	reference.K1 = SpeedCalculator<double>::ComputeK1(baseFrequency, ts);
	calc.K2 = T(0.95);
	calc.K3 = T(0.05);
	reference.K2 = Q<T>(0.95);
	reference.K3 = Q<T>(0.05);

	double maxSpeed = 0;
	double maxRpm = 0;
	double maxRpmProduct = 0;
	// One differentiator step of one angle LSB, which is the quantum of the speed. The mean over the settled half averages
	// the rounding of the angle, so it stays within one such step of rate / fb.
	double settledTolerance = FOCCheck::ToDouble(calc.K1) * Lsb<T>();
	double sumSpeed = 0;
	int settledCount = 0;
	for (int k = 0; k < steps; k++) {
		double angle = rateHz * k * ts + 0.0123;
		double electricalAngle = Q<T>(angle - std::floor(angle));

		calc.Set(T(electricalAngle)).Resolve();
		reference.Set(electricalAngle).Resolve();
		double speed = FOCCheck::ToDouble(calc.Get().speed);
		if (k >= settleStart && k < settleEnd) {
			sumSpeed += speed;
			settledCount++;
		}
		maxSpeed = std::fmax(maxSpeed, std::fabs(speed - reference.Get().speed));
		maxRpm = std::fmax(maxRpm, std::fabs(static_cast<double>(calc.Get().speedRpm) - reference.Get().speedRpm));
		// The rpm is the speed times baseRpm, truncated toward zero: within one count of the product.
		maxRpmProduct = std::fmax(maxRpmProduct, std::fabs(static_cast<double>(calc.Get().speedRpm) - speed * baseRpm));
	}

	char label[96];
	std::snprintf(label, sizeof label, "SpeedCalculator speed %s", name);
	FOCCheck::Report(label, maxSpeed, bound, __LINE__);
	std::snprintf(label, sizeof label, "SpeedCalculator rpm against the reference %s", name);
	FOCCheck::Report(label, maxRpm, bound * baseRpm + 1.0, __LINE__);
	std::snprintf(label, sizeof label, "SpeedCalculator rpm against the speed %s", name);
	FOCCheck::Report(label, maxRpmProduct, 1.0, __LINE__);

	// Physical check, independent of the reference: a steady rotation of rateHz at fb has speed rateHz / fb in pu.
	// The window is past the filter's start-up and holds whole turns, so the telescoping sum of the angle steps is exact.
	// The tolerance covers the quantization ripple of the angle, which the low-pass filter averages.
	double settledSpeed = sumSpeed / settledCount;
	std::snprintf(label, sizeof label, "SpeedCalculator settled speed is rate / fb %s", name);
	FOCCheck::Report(label, std::fabs(settledSpeed - rateHz / baseFrequency), settledTolerance, __LINE__);
}


// Timer counts from a free-running timer: the difference wraps at maximumOutput, so the wrap path runs too.
template<typename T>
void SpeedPeriodChecks(const char* name, double bound) {
	SpeedPeriodCalculator<T> calc(T(260.0), T(1800.0), T(0x7FFF), SpeedPeriodCalculator<T>::InputSelect::TimeStamp);
	SpeedPeriodCalculator<double> reference(260.0, 1800.0, 0x7FFF, SpeedPeriodCalculator<double>::InputSelect::TimeStamp);
	double maxSpeed = 0;
	double maxRpm = 0;
	unsigned stamp = 0;
	for (int k = 0; k < steps; k++) {
		stamp = (stamp + 37 + (k % 11)) % 32768;
		calc.SetTimeStamp(T(stamp)).Resolve();
		reference.SetTimeStamp(double(stamp)).Resolve();
		maxSpeed = std::fmax(maxSpeed, std::fabs(FOCCheck::ToDouble(calc.Get().speed) - reference.Get().speed));
		maxRpm = std::fmax(maxRpm, std::fabs(FOCCheck::ToDouble(calc.Get().speedRpm) - reference.Get().speedRpm));
	}

	char label[96];
	std::snprintf(label, sizeof label, "SpeedPeriodCalculator timestamp speed %s", name);
	FOCCheck::Report(label, maxSpeed, bound, __LINE__);
	std::snprintf(label, sizeof label, "SpeedPeriodCalculator timestamp rpm %s", name);
	FOCCheck::Report(label, maxRpm, bound * 1800.0, __LINE__);

	SpeedPeriodCalculator<T> periodCalc(T(260.0), T(1800.0), T(0x7FFF), SpeedPeriodCalculator<T>::InputSelect::EventPeriod);
	SpeedPeriodCalculator<double> periodReference(260.0, 1800.0, 0x7FFF, SpeedPeriodCalculator<double>::InputSelect::EventPeriod);
	maxSpeed = 0;
	for (int k = 0; k < steps; k++) {
		double period = 300 + (k * 7) % 900;
		periodCalc.SetEventPeriod(T(period)).Resolve();
		periodReference.SetEventPeriod(period).Resolve();
		maxSpeed = std::fmax(maxSpeed, std::fabs(FOCCheck::ToDouble(periodCalc.Get().speed) - periodReference.Get().speed));
	}
	std::snprintf(label, sizeof label, "SpeedPeriodCalculator event period speed %s", name);
	FOCCheck::Report(label, maxSpeed, bound, __LINE__);
}

// The rpm of a fast event: 10 counts give 26 pu and 46800 rpm, which is above the range of IQ16 (32768). Float holds it,
// and the IQ rpm is an integer that must hold it too, so neither may saturate.
template<typename T>
void SpeedPeriodRpmChecks(const char* name) {
	SpeedPeriodCalculator<T> calc(T(260.0), T(1800.0), T(0x7FFF), SpeedPeriodCalculator<T>::InputSelect::EventPeriod);
	calc.SetEventPeriod(T(10.0)).Resolve();
	char label[96];
	std::snprintf(label, sizeof label, "SpeedPeriodCalculator rpm above the IQ16 range %s", name);
	FOCCheck::Report(label, std::fabs(FOCCheck::ToDouble(calc.Get().speedRpm) - 46800.0), 1.0, __LINE__);
}

}


// SpeedPeriodCalculator is constrained by RangeAtLeast<T, 32767>, so these predicates decide what it accepts. A wider
// Q is also rejected by the class itself at compile time (GCC reports the failed constraint, it does not return false
// from a requires-expression, so the class is not instantiated here).
static_assert(RangeAtLeast<float, 32767.0>, "float holds the timer counts");
static_assert(RangeAtLeast<IQ<16>, 32767.0>, "IQ16 holds the timer counts");
static_assert(!RangeAtLeast<IQ<17>, 32767.0>, "IQ17 does not hold the timer counts");
static_assert(!RangeAtLeast<IQ<24>, 32767.0>, "IQ24 does not hold the timer counts");
static_assert(RangeAtLeast<IQ<24>, 128.0>, "IQ24 holds K1 = 100 (fb 100 Hz at Ts 100 us)");


int main() {
	SpeedPeriodRpmChecks<float>("float");
	SpeedPeriodRpmChecks<IQ<16>>("IQ16");

	ComputeK1Check<float>("float, fb 50 Hz", 50.0, 1e-7);
	// Ts = 100 us goes in as a double and K1 is computed in float for IQ; the result must be within half an IQ16 LSB of 200.
	ComputeK1Check<IQ<16>>("IQ16, fb 50 Hz", 50.0, 0.5 / (1 << 16));
	ComputeK1Check<IQ<22>>("IQ22, fb 50 Hz", 50.0, 0.5 / (1 << 22));
	ComputeK1Check<IQ<24>>("IQ24, fb 100 Hz", 100.0, 0.5 / (1 << 24));

	// fb 50 Hz, rotation 25 Hz: speed 0.5 pu, K1 = 200 needs IQ22 or lower.
	SpeedChecks<float>("float, fb 50 Hz", 1e-5, 50.0, 25.0, 3600);
	SpeedChecks<IQ<16>>("IQ16, fb 50 Hz", 1e-2, 50.0, 25.0, 3600);
	SpeedChecks<IQ<20>>("IQ20, fb 50 Hz", 1e-3, 50.0, 25.0, 3600);
	SpeedChecks<IQ<22>>("IQ22, fb 50 Hz", 1e-3, 50.0, 25.0, 3600);
	// fb 100 Hz gives K1 = 100, which fits IQ24. The rotation of 50 Hz is speed 0.5 pu, 1800 rpm: a 32-bit product at
	// Q24 would saturate at 128 rpm.
	SpeedChecks<IQ<24>>("IQ24, fb 100 Hz", 1e-4, 100.0, 50.0, 3600);

	SpeedPeriodChecks<float>("float", 1e-5);
	SpeedPeriodChecks<IQ<16>>("IQ16", 1e-3);

	return FOCCheck::Finish();
}
