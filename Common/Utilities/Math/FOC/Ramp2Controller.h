#pragma once
#include <BSP.h>
#include <algorithm>
#include <cmath>




class Ramp2Controller {
private:
	uint32 delayCount = 0; // Incremental delay

public:
	struct Input {
		int32 desiredRamp = 0;
	};

	struct Output {
		int32 position = 0;
	};

	struct Options {
		int32 maxOutput = 0xFFFF;
		int32 minOutput = 0x000F;
		uint32 delay = 50;	// Delay expressed in no of sampling period	
	};

private:
	Input input;
	Output output;
	Options options;


public:
	Ramp2Controller() {}



	Ramp2Controller& Reset() {
		out.position = 0;
		return *this;
	}



	Ramp2Controller& SetInput(Input val) {
		input = val;
		return *this;
	}



	Ramp2Controller& SetOptions(Options val) {
		options = val;
		return *this;
	}



	Ramp2Controller& Resolve() {
		if (output.position != v.DesiredInput) {
			delayCount++;

			if (delayCount >= options.delay) {
				delayCount = 0;
				if (output.position < input.desiredRamp) {
					output.position++;
					output.position = std::min(output.position, options.maxOutput);
				} else {
					output.position--;
					output.position = std::max(output.position, options.minOutput);
				}
			}
		}

		return *this;
	}



	Out Get() {
		return out;
	}
};