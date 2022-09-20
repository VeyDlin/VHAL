#pragma once
#include <BSP.h>



class Impulse {
private:
	uint32 counter = 0;
	bool impulseFlag = false;
	uint32 period = 1000; // Period of output impulse in a number of sampling cycles


public:
	Impulse() { }

	Impulse(float impulsePeriod): period(impulsePeriod) { }



	Impulse& SetPeriod(float val) {
		period = val;
		return *this;
	}



	Impulse& Resolve() {
		impulseFlag = false; // Always clear impulse output at entry

		if (++counter >= period) {
			impulseFlag = true;
			counter = 0;
		}

		return *this;
	}



	bool IsImpulse() {
		return impulseFlag;
	}

};