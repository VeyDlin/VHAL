#pragma once
#include <VHAL.h>
#include <concepts>


// Fires once every period calls of Resolve. Range: the period is a count of sampling cycles, uint32 (up to 2^32 - 1), so it
// is not a T and does not depend on the number format.
class Impulse {
private:
	uint32 counter = 0;
	bool impulseFlag = false;
	uint32 period = 1000; // Period of output impulse in a number of sampling cycles


public:
	Impulse() { }

	Impulse(uint32 impulsePeriod): period(impulsePeriod) { }

	// A float period would be truncated to a count, so it does not compile.
	template<std::floating_point F>
	Impulse(F) = delete;



	Impulse& SetPeriod(uint32 val) {
		period = val;
		return *this;
	}


	template<std::floating_point F>
	Impulse& SetPeriod(F) = delete;



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