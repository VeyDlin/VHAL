#pragma once
#include "../reductionTypes.h"
#include <algorithm>
#include <cmath>


class LowPassFilter {
private:
	float kx0 = 0;
	float ky1 = 0;
	float input = 0;
	float output = 0;

	struct {
		float frequencyTime = 10; // (1 / cutoffFrequency)
	  	float samplingTime = 10;
	} in;

public:
  	LowPassFilter() { }

	LowPassFilter(float frequencyTime, float samplingTime) {
		in.frequencyTime = frequencyTime;
		in.samplingTime = samplingTime;
		UpdateCoefficients();
	}


	LowPassFilter& Set(float data) {
		input = data;
		return *this;
	}

	LowPassFilter& Resolve() {
		output = (kx0 * input) + (ky1 * output);
		return *this;
	}


	float Get() {
		return output;
	}


private:
	void UpdateCoefficients() {
		in.frequencyTime /= in.samplingTime;
		ky1 = in.frequencyTime/ (in.frequencyTime + 1.0);
		kx0 = 1.0 / (in.frequencyTime + 1.0);
	}
};



