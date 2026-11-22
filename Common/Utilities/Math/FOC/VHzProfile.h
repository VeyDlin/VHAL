#pragma once
#include <VHAL.h>
#include <cmath>


// Volts per hertz profile: voltMin below lowFreq, a straight line up to highFreq and voltMax above. T is float or IQ<Q>.
// Range: the frequencies and voltages are per-unit values below 2^(31 - Q) in magnitude, and the slope
// (voltMax - voltMin) / (highFreq - lowFreq) must also fit in that range. There is no fixed range: the slope depends on
// the parameters, so the user checks it for the parameters in use.
template<typename T = float>
class VHzProfile {
private:
	struct {
		T frequency = 0; // Input Frequency (pu)
	} in;


public:
	struct {
		T lowFreq = 0;   // Low Frequency (pu)
		T highFreq = 0;  // High Frequency at rated voltage (pu)
		T voltMax = 0;   // Rated voltage (pu)
		T voltMin = 0;   // Voltage at low Frequency range (pu)
	} parameters;

	struct Out {
		T voltage = 0; // Output voltage (pu)
	} out;


	VHzProfile& Set(T frequency) {
		in.frequency = frequency;
		return *this;
	}


	VHzProfile& Resolve() {
		using std::abs;

		T absFreq = abs(in.frequency);

		if (absFreq <= parameters.lowFreq) {
			out.voltage = parameters.voltMin;
		} else if (absFreq <= parameters.highFreq) {
			T vfSlope = (parameters.voltMax - parameters.voltMin) / (parameters.highFreq - parameters.lowFreq);
			out.voltage = parameters.voltMin + vfSlope * (absFreq - parameters.lowFreq);
		} else {
			out.voltage = parameters.voltMax;
		}

		return *this;
	}


	Out Get() {
		return out;
	}
};
