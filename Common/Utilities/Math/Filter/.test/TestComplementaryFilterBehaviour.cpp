#include <VHAL.h>
#include <Utilities/Math/Filter/ComplementaryFilter.h>
#include <VhalCheck.h>
#include <cmath>


// Behaviour of ComplementaryFilter<float>: the angles from gravity at rest, the convergence of a roll, the gyroscope alone while the
// accelerometer is not trusted, the reset, and the bias error that the time constant sets.


namespace {


constexpr float pi = 3.14159265358979f;


// Gravity seen by a sensor pitched by the angle (rotation about Y): the accelerometer reads -sin and cos of it.
ComplementaryFilter<float>::Sample PitchSample(float degrees) {
	float radians = degrees * pi / 180.0f;
	ComplementaryFilter<float>::Sample sample;
	sample.accelX = -std::sin(radians);
	sample.accelZ = std::cos(radians);
	return sample;
}


// Gravity seen by a sensor rolled by the angle (rotation about X).
ComplementaryFilter<float>::Sample RollSample(float degrees) {
	float radians = degrees * pi / 180.0f;
	ComplementaryFilter<float>::Sample sample;
	sample.accelY = std::sin(radians);
	sample.accelZ = std::cos(radians);
	return sample;
}


void CheckStaticGravity() {
	ComplementaryFilter<float> filter;
	filter.Reset(PitchSample(10.0f));
	for (uint32 step = 0; step < 300; step++) {
		filter.Update(PitchSample(10.0f), 0.01f);
	}
	VHAL_CHECK_NEAR(filter.GetPitchDeg(), 10.0f, 0.01f);
	VHAL_CHECK_NEAR(filter.GetRollDeg(), 0.0f, 0.01f);
}


// Starting level, then a 20 degree roll with no gyroscope input: the time constant (1 s) pulls the roll to gravity.
void CheckRollConverges() {
	ComplementaryFilter<float> filter;
	filter.Reset(PitchSample(0.0f));
	for (uint32 step = 0; step < 500; step++) {
		filter.Update(RollSample(20.0f), 0.01f);
	}
	VHAL_CHECK_NEAR(filter.GetRollDeg(), 20.0f, 0.5f);
	VHAL_CHECK_NEAR(filter.GetPitchDeg(), 0.0f, 0.5f);
}


// The accelerometer reads 2 g, so its correction has no weight and the gyroscope alone moves the angle: 10 deg/s for
// 0.5 s is 5 deg.
void CheckGyroOnlyWhileAccelerating() {
	ComplementaryFilter<float> filter;
	filter.Reset(PitchSample(0.0f));
	ComplementaryFilter<float>::Sample sample;
	sample.accelZ = 2.0f;
	sample.gyroY = 10.0f;
	for (uint32 step = 0; step < 50; step++) {
		filter.Update(sample, 0.01f);
	}
	VHAL_CHECK_NEAR(filter.GetPitchDeg(), 5.0f, 0.01f);
}


void CheckResetReseeds() {
	ComplementaryFilter<float> filter;
	filter.Reset(PitchSample(0.0f));
	for (uint32 step = 0; step < 100; step++) {
		filter.Update(RollSample(30.0f), 0.01f);
	}
	filter.Reset(PitchSample(0.0f));
	VHAL_CHECK_NEAR(filter.GetRollDeg(), 0.0f, 0.001f);
	VHAL_CHECK_NEAR(filter.GetPitchDeg(), 0.0f, 0.001f);
}


// A gyroscope bias of 0.5 deg/s at rest settles at bias * time constant. The time constant is set to 0.5 s here, so the
// error is 0.25 deg.
void CheckGyroBiasSettles() {
	ComplementaryFilter<float> filter;
	ComplementaryFilter<float>::Tuning tuning;
	tuning.timeConstantSec = 0.5f;
	filter.SetTuning(tuning);
	filter.Reset(PitchSample(0.0f));

	ComplementaryFilter<float>::Sample sample = PitchSample(0.0f);
	sample.gyroY = 0.5f;
	for (uint32 step = 0; step < 1000; step++) {
		filter.Update(sample, 0.01f);
	}
	VHAL_CHECK_NEAR(filter.GetPitchDeg(), 0.25f, 0.03f);
}


}


int main() {
	CheckStaticGravity();
	CheckRollConverges();
	CheckGyroOnlyWhileAccelerating();
	CheckResetReseeds();
	CheckGyroBiasSettles();
	return VhalCheck::Finish("TestComplementaryFilterBehaviour");
}
