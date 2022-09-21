#pragma once
#include <algorithm>
#include <cmath>

// TODO: Ref: rampgen.h

class RampGenerator {
private:
	struct {
		float rampFrequency = 0;
		float gain = 1;
		float offset = 1;
	} in;

	float outputRampSignal = 0;
	float angle = 0;
public:
	float stepAngleMax = 0;
	float outputMax = 1;
	float outputMin = 0;


	RampGenerator& Set(float rampFrequency, float gain, float offset) {
		in.rampFrequency = rampFrequency;
		in.gain = gain;
		in.offset = offset;
		return *this;
	}


	RampGenerator& Resolve() {
		// Compute the angle rate
		angle += stepAngleMax * in.rampFrequency;

		// Saturate the angle rate within (-1, 1)
		if (angle > outputMax) {
			angle = outputMin;
		} else if (angle < outputMin) {
			angle = outputMax;
		}

		// Compute the ramp output
		outputRampSignal = (angle * in.gain) + in.offset;

		// Saturate the ramp output within (-1, 1)
		if (outputRampSignal > outputMax) {
			outputRampSignal = outputMin;
		} else if (outputRampSignal < outputMin) {
			outputRampSignal = outputMax;
		}

		return *this;
	}


	float Get() {
		return outputRampSignal;
	}
};




