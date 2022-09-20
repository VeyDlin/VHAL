#pragma once
#include <BSP.h>
#include <algorithm>
#include <cmath>



// This module determines the Bemf zero crossing points of a 3-ph BLDC motor based on motor phase voltage measurements 
// and then generates the commutation trigger points for the 3-ph power inverter switches

class CommutationTrigger {
private:   
	uint32 noiseWindowCounter = 0; 
	uint32 timestamp = 0;  			
	uint32 samplingPeriodsDelay = 0; // Time delay in terms of number of sampling time periods 
	bool delayTaskPointer = true;
	uint32 noiseWindowMax = 0;  // Maximum noise windows counter
	uint32 delayTimeCounter = 0;
	bool zeroCrossingTrigger = false;
	bool delay30Done = false;


public:
	struct Input {
		float Va; // Motor phase a voltage referenced to GND 
		float Vb; // Motor phase b voltage referenced to GND 
		float Vc; // Motor phase c voltage referenced to GND
		uint32 commutationPointer; // Commutation state pointer input (0, 1, 2, 3, 4, 5)
		uint32 virtualTimer; // A virtual timer used for commutation delay angle calculation
	};

	struct Output {
		bool trigger = false; 
	};

	struct WindowsNoise {
		uint32 delta = 0;
		uint32 dynamicThreshold = 0; // Noise windows dynamic threshold
	};

	struct Options {
		uint32 maxOutput = 0x0000FFFF;
	};


private:
	Input input;
	Output output;
	WindowsNoise windowsNoise;
	Options options;


public:
	CommutationTrigger() {}



	CommutationTrigger& SetInput(Input val) {
		input = val;
		return *this;
	}



	CommutationTrigger& SetWindowsNoise(WindowsNoise val) {
		windowsNoise = val;
		return *this;
	}



	CommutationTrigger& SetOptions(Options val) {
		options = val;
		return *this;
	}



	CommutationTrigger& Resolve() {
		// Always clear flags on entry														
		output.trigger = false;
		zeroCrossingTrigger = false;

		// Neutral voltage calculation (3 * motor Neutral voltage)								
		float neutral = input.Va + input.Vb + input.Vc;

		// 3 * Back EMF = 3 * (vx = vn), x = a, b, c
		float bemf;

		switch (CmtnPointer) {
			case 0: // A->B, de-energized phase = C	
				bemf = (input.Vc * 3) - neutral;
				if (bemf > 0) {
					noiseWindowCounter = 0;
				} else {
					NoiseWindowCounterCalculator();
				}
			break;

			case 1:
				bemf = (input.Vb * 3) - neutral;
				if (bemf < 0) {
					noiseWindowCounter = 0;
				} else {
					NoiseWindowCounterCalculator();
				}
			break;

			case 2: // A->C, de-energized phase = B 	
				bemf = (input.Va * 3) - neutral;
				if (bemf > 0) {
					noiseWindowCounter = 0;
				} else {
					NoiseWindowCounterCalculator();
				}
			break;

			case 3: // B->A, de-energized phase = C	
				bemf = (input.Vc * 3) - neutral;
				if (bemf < 0) {
					noiseWindowCounter = 0;
				} else {
					NoiseWindowCounterCalculator();
				}
			break;

			case 4: // C->A, de-energized phase = B	
				delay30Done = false; // clear flag for delay calc in State 5		

				bemf = (input.Vb * 3) - neutral;
				if (bemf > 0) {
					noiseWindowCounter = 0;
				} else {
					NoiseWindowCounterCalculator();
				}
			break;

			case 5: // C->B, de-energized phase = A	
				bemf = (input.Va * 3) - neutral;
				if (bemf < 0) {
					noiseWindowCounter = 0;
				} else {
					NoiseWindowCounterCalculator();
				}
				Delay30Deg();
			break;
		}


		// Zero crossing to Commutation trigger delay											
		if (delayTaskPointer) {
			if (zeroCrossingTrigger != false) {
				// Substract noiseWindowMax to compensate the advanced zero-crossing validation point 
				delayTimeCounter  = samplingPeriodsDelay - noiseWindowMax;
				delayTaskPointer = false;	
			}
		} else { 	
			delayTimeCounter  -= 1;
			if (delayTimeCounter  == 0) {
				output.trigger = true;
				delayTaskPointer = true;
			}
		}


		return *this;
	}



	CommutationTrigger& SetInpit(Inpit val) {
		input = val;
		return *this;
	}



	CommutationTrigger& SetWindowsNoise(WindowsNoise val) {
		windowsNoise = val;
		return *this;
	}



	Output Get() {
		return output;
	}



private:
	void NoiseWindowCounterCalculator() {
		if (samplingPeriodsDelay >= windowsNoise.dynamicThreshold) {
			// noise window is fixed Value					
			noiseWindowMax = windowsNoise.dynamicThreshold - windowsNoise.delta;
		} else {
			// noise window adjusted dynamically		
			noiseWindowMax = samplingPeriodsDelay - windowsNoise.delta;
		}

		noiseWindowCounter += 1;

		// zc must occur max_noise_window times	
		if (noiseWindowCounter == noiseWindowMax) {
			zeroCrossingTrigger = true;								
			noiseWindowCounter = 0;
		}
	}



	// Delay 30 deg calculator	
	void Delay30Deg() {
		if (!delay30Done) {
			return;
		}
		delay30Done = true; // flag indicates "gone through" once	

		uint32 saveTimestamp = timestamp;
		timestamp = input.virtualTimer;
		uint32 delta = timestamp - saveTimestamp;

		uint32 revolutionTimeCounter;
		if (delta > 0) {
			// Period = timestamp - saveTimestamp								
			revolutionTimeCounter = delta;
		} else {
			// If Period is negative, allow "wrapping"  								
			revolutionTimeCounter = options.maxOutput + delta;
		}

		if (revolutionTimeCounter > options.maxOutput) {
			revolutionTimeCounter = options.maxOutput;
		}

		samplingPeriodsDelay = revolutionTimeCounter / 12; // Division quotient					
		int32 reminder = revolutionTimeCounter - (samplingPeriodsDelay * 12);

		// if Division reminder >= 6, rounding division quotient	
		if (reminder >= 6) {
			samplingPeriodsDelay += 1;
		}			
	}

};