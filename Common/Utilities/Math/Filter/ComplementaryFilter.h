#pragma once
#include <Utilities/DataTypes.h>
#include <Utilities/Math/IQMath/IQMath.h>
#include <Utilities/Math/NumberRange.h>
#include <algorithm>
#include <cmath>
#include <concepts>


// Pitch and roll of a body from one accelerometer and one gyroscope. The gyroscope is integrated, and the gravity
// direction from the accelerometer corrects the drift slowly (the time constant). While the body accelerates, the
// accelerometer measures more than gravity, so its correction fades out as the acceleration norm leaves 1 g.
// Pitch is the rotation about the sensor's Y axis and roll the rotation about X; both are right-handed, so the gyroscope
// rate of an axis is the derivative of its angle. The filter is clock-free: the caller passes the time between samples.
// The number type must hold the 180 degree angle, so IQ<24> (range 128) is rejected at compile time. The gyroscope rate is
// the largest input and its full scale depends on the sensor, so the caller picks a type that holds it: IQ<20> (range 2048)
// for a 2000 deg/s full scale, IQ<23> (range 256) for 250 deg/s. Inputs beyond the range saturate.
template<RealType T = float>
	requires RangeAtLeast<T, 180.0>
class ComplementaryFilter {
public:
	// Accelerometer in g, gyroscope in deg/s (bias already removed by the caller).
	struct Sample {
		T accelX = T(0);
		T accelY = T(0);
		T accelZ = T(0);
		T gyroX = T(0);
		T gyroY = T(0);
		T gyroZ = T(0);
	};


	struct Tuning {
		// The gravity correction takes over after about this long (s). A constant gyroscope bias b settles at an angle
		// error of about b * timeConstantSec.
		T timeConstantSec = T(1);
		// Corner of the low-pass filter on the accelerometer angles (Hz). It removes the vibration of the accelerometer.
		T accelLowPassHz = T(10);
		// The correction is weighted down by |accel| - 1 g beyond this window. Must be greater than zero.
		T accelTrustG = T(0.25);
	};


private:
	static constexpr T radToDeg = static_cast<T>(57.29577951);
	static constexpr T twoPi = static_cast<T>(6.28318531);

	Tuning tuning;
	T pitchDeg = T(0);
	T rollDeg = T(0);
	T accelPitchLowDeg = T(0);
	T accelRollLowDeg = T(0);


public:
	void SetTuning(const Tuning& value) {
		tuning = value;
	}


	// Starts from the gravity direction of the sample. Call it for the first sample and after a gap in the samples.
	void Reset(const Sample& sample) {
		T accelPitch = T(0);
		T accelRoll = T(0);
		AccelAngles(sample, accelPitch, accelRoll);
		pitchDeg = accelPitch;
		rollDeg = accelRoll;
		accelPitchLowDeg = accelPitch;
		accelRollLowDeg = accelRoll;
	}


	// Feeds one sample, dtSec after the previous one. dtSec must be greater than zero.
	void Update(const Sample& sample, T dtSec) {
		using std::exp;
		using std::sqrt;
		using std::abs;

		T accelPitch = T(0);
		T accelRoll = T(0);
		AccelAngles(sample, accelPitch, accelRoll);

		T lowAlpha = T(1) - exp(-twoPi * tuning.accelLowPassHz * dtSec);
		accelPitchLowDeg = accelPitchLowDeg + lowAlpha * (accelPitch - accelPitchLowDeg);
		accelRollLowDeg = accelRollLowDeg + lowAlpha * (accelRoll - accelRollLowDeg);

		T accelNorm = sqrt(sample.accelX * sample.accelX + sample.accelY * sample.accelY
			+ sample.accelZ * sample.accelZ);
		T trust = T(1) - abs(accelNorm - T(1)) / tuning.accelTrustG;
		trust = std::clamp(trust, T(0), T(1));

		T correction = trust * dtSec / (tuning.timeConstantSec + dtSec);
		pitchDeg = (pitchDeg + sample.gyroY * dtSec) * (T(1) - correction) + accelPitchLowDeg * correction;
		rollDeg = (rollDeg + sample.gyroX * dtSec) * (T(1) - correction) + accelRollLowDeg * correction;
	}


	T GetPitchDeg() const {
		return pitchDeg;
	}


	T GetRollDeg() const {
		return rollDeg;
	}


private:
	static void AccelAngles(const Sample& sample, T& pitch, T& roll) {
		using std::sqrt;
		using std::atan2;

		T yz = sqrt(sample.accelY * sample.accelY + sample.accelZ * sample.accelZ);
		pitch = atan2(-sample.accelX, yz) * radToDeg;
		roll = atan2(sample.accelY, sample.accelZ) * radToDeg;
	}
};
