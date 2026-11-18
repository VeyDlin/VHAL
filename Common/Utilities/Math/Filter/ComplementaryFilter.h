#pragma once
#include <Utilities/DataTypes.h>
#include <algorithm>
#include <cmath>


// Pitch and roll of a body from one accelerometer and one gyroscope. The gyroscope is integrated, and the gravity
// direction from the accelerometer corrects the drift slowly (the time constant). While the body accelerates, the
// accelerometer measures more than gravity, so its correction fades out as the acceleration norm leaves 1 g.
// Pitch is the rotation about the sensor's Y axis and roll the rotation about X; both are right-handed, so the gyroscope
// rate of an axis is the derivative of its angle. The filter is clock-free: the caller passes the time between samples.
class ComplementaryFilter {
public:
	// Accelerometer in g, gyroscope in deg/s (bias already removed by the caller).
	struct Sample {
		float accelX = 0.0f;
		float accelY = 0.0f;
		float accelZ = 0.0f;
		float gyroX = 0.0f;
		float gyroY = 0.0f;
		float gyroZ = 0.0f;
	};


	struct Tuning {
		// The gravity correction takes over after about this long (s). A constant gyroscope bias b settles at an angle
		// error of about b * timeConstantSec.
		float timeConstantSec = 1.0f;
		// Corner of the low-pass filter on the accelerometer angles (Hz). It removes the vibration of the accelerometer.
		float accelLowPassHz = 10.0f;
		// The correction is weighted down by |accel| - 1 g beyond this window. Must be greater than zero.
		float accelTrustG = 0.25f;
	};


private:
	static constexpr float radToDeg = 57.29577951f;
	static constexpr float twoPi = 6.28318531f;

	Tuning tuning;
	float pitchDeg = 0.0f;
	float rollDeg = 0.0f;
	float accelPitchLowDeg = 0.0f;
	float accelRollLowDeg = 0.0f;


public:
	void SetTuning(const Tuning& value) {
		tuning = value;
	}


	// Starts from the gravity direction of the sample. Call it for the first sample and after a gap in the samples.
	void Reset(const Sample& sample) {
		float accelPitch = 0.0f;
		float accelRoll = 0.0f;
		AccelAngles(sample, accelPitch, accelRoll);
		pitchDeg = accelPitch;
		rollDeg = accelRoll;
		accelPitchLowDeg = accelPitch;
		accelRollLowDeg = accelRoll;
	}


	// Feeds one sample, dtSec after the previous one. dtSec must be greater than zero.
	void Update(const Sample& sample, float dtSec) {
		float accelPitch = 0.0f;
		float accelRoll = 0.0f;
		AccelAngles(sample, accelPitch, accelRoll);

		float lowAlpha = 1.0f - std::exp(-twoPi * tuning.accelLowPassHz * dtSec);
		accelPitchLowDeg = accelPitchLowDeg + lowAlpha * (accelPitch - accelPitchLowDeg);
		accelRollLowDeg = accelRollLowDeg + lowAlpha * (accelRoll - accelRollLowDeg);

		float accelNorm = std::sqrt(sample.accelX * sample.accelX + sample.accelY * sample.accelY
			+ sample.accelZ * sample.accelZ);
		float trust = 1.0f - std::fabs(accelNorm - 1.0f) / tuning.accelTrustG;
		trust = std::clamp(trust, 0.0f, 1.0f);

		float correction = trust * dtSec / (tuning.timeConstantSec + dtSec);
		pitchDeg = (pitchDeg + sample.gyroY * dtSec) * (1.0f - correction) + accelPitchLowDeg * correction;
		rollDeg = (rollDeg + sample.gyroX * dtSec) * (1.0f - correction) + accelRollLowDeg * correction;
	}


	float GetPitchDeg() const {
		return pitchDeg;
	}


	float GetRollDeg() const {
		return rollDeg;
	}


private:
	static void AccelAngles(const Sample& sample, float& pitch, float& roll) {
		float yz = std::sqrt(sample.accelY * sample.accelY + sample.accelZ * sample.accelZ);
		pitch = std::atan2(-sample.accelX, yz) * radToDeg;
		roll = std::atan2(sample.accelY, sample.accelZ) * radToDeg;
	}
};
