#pragma once
#include <VHAL.h>
#include <algorithm>
#include <Utilities/Math/IQMath/IQ.h>


template <RealType Type = float>
class LowPassFilter {
private:
	Type kx0 = 0;
	Type ky1 = 0;
	Type input = 0;
	Type output = 0;

	struct {
		Type frequencyTime = 10; // (1 / cutoffFrequency)
	  	Type samplingTime = 10;
	} in;

public:
  	LowPassFilter() { }

	LowPassFilter(Type frequencyTime, Type samplingTime) {
		in.frequencyTime = frequencyTime;
		in.samplingTime = samplingTime;
		UpdateCoefficients();
	}


	LowPassFilter& Set(Type data) {
		input = data;
		return *this;
	}

	LowPassFilter& Resolve() {
		output = (kx0 * input) + (ky1 * output);
		return *this;
	}


	Type Get() {
		return output;
	}


private:
	void UpdateCoefficients() {
		in.frequencyTime /= in.samplingTime;
		ky1 = in.frequencyTime / (in.frequencyTime + Type(1));
		kx0 = Type(1) / (in.frequencyTime + Type(1));
	}
};