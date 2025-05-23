#pragma once
#include "../reductionTypes.h"
#include <algorithm>
#include <cmath>




class Ramp3Controller {
private:
	uint32 delayCount = 0;

	struct {
		int32 desiredInput;
	} in;



public:
    struct {
    	uint32 delay = 0; // Delay expressed in no of sampling period
    	int32 minimumOutput = 0x0050;
    } parameters;


	struct Out {
    	int32 position = 0;
    	bool done = false;
	} out;


	Ramp3Controller() { }

	Ramp3Controller(uint32 delay, int32 minimumOutput, int32 startPosition = 0) {
		parameters.delay = delay;
		parameters.minimumOutput = minimumOutput;
		out.position = startPosition;
	}


	Ramp3Controller& Reset(int32 startPosition = 0) {
		out.position = startPosition;
		out.done = false;
		return *this;
	}


	Ramp3Controller& Set(float desiredInput) {
		in.desiredInput = desiredInput;
		return *this;
	}


	Ramp3Controller& Resolve() {
		if (out.position == in.desiredInput) {
			out.done = true;
		} else {
			if(++delayCount >= parameters.delay) {
				if(--out.position < parameters.minimumOutput) {
					out.position = parameters.minimumOutput;
				}
				delayCount = 0;
			}
		}

		return *this;
	}


	Out Get() {
		return out;
	}
};








