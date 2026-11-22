#pragma once
#include <algorithm>
#include <cmath>


// A ramp of per-unit values, one step of rampFrequency per call, between offset and gain. T is float or IQ<Q>.
// Range: rampFrequency, gain and offset are per-unit values. The output is angle * gain + offset with the angle within
// outputMin and outputMax, so |gain| + |offset| must be below 2^(31 - Q) in magnitude.
template<typename T = float>
class RampGenerator {
private:
	struct {
		T rampFrequency = 0;
		T gain = 1;
		T offset = 1;
	} in;

	T outputRampSignal = 0;
	T angle = 0;
public:
	T stepAngleMax = 0;
	T outputMax = 1;
	T outputMin = 0;


	RampGenerator& Set(T rampFrequency, T gain, T offset) {
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


	T Get() {
		return outputRampSignal;
	}
};
