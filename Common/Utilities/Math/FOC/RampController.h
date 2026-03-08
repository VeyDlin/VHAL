#pragma once
#include <VHAL.h>
#include <algorithm>
#include <cmath>


template<typename T = float>
class RampController {
private:
	T targetPosition = 0;
	uint32 delayCount = 0;


public:
    struct {
    	uint32 delayMax = 5;
    	T amplitudeMin = -1;
    	T amplitudeMax = 1;
		T resolution = T(0.0000305);
    } parameters;


	struct Out {
		T position = 0;
		bool equalFlag = false;
	} out;


	RampController() { }

	RampController(uint32 delayMax, T amplitudeMin, T amplitudeMax, T resolution, int32 startPosition = 0) {
		parameters.delayMax = delayMax;
		parameters.amplitudeMin = amplitudeMin;
		parameters.amplitudeMax = amplitudeMax;
		parameters.resolution = resolution;
		out.position = startPosition;
	}


	RampController& Reset(int32 startPosition = 0) {
		out.position = startPosition;
		out.equalFlag = false;
		return *this;
	}


	RampController& SetTargetPosition(T target) {
		targetPosition = target;
		return *this;
	}

	RampController& SetReferencePosition(T position) {
		out.position = position;
		return *this;
	}



	RampController& Resolve() {
		T dlt = targetPosition - out.position;

		using std::abs;
		if (abs(dlt) >= parameters.resolution) {
			delayCount++;

			if (delayCount >= parameters.delayMax) {
				if (targetPosition >= out.position) {
					out.position += parameters.resolution;
				} else {
					out.position -= parameters.resolution;
				}

				using std::min;
				using std::max;
				out.position = min(max(out.position , parameters.amplitudeMin), parameters.amplitudeMax);

				delayCount = 0;
			}
		} else {
			out.equalFlag = true;
			out.position = targetPosition;
		}

		return *this;
	}


	Out Get() {
		return out;
	}

	T GetTargetPosition() {
		return targetPosition;
	}



	// Slew programmable ramper
	static inline T Ramper(T in, T out, T rampDelta) {
		T err = in - out;

		if (err > rampDelta) {
			return out + rampDelta;
		} else if (err < -rampDelta) {
	  		return out - rampDelta;
		}

	    return in;
	}
};
