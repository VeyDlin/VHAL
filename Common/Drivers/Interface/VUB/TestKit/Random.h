#pragma once
#include <Utilities/DataTypes.h>

namespace VUB {


// A small deterministic generator (xorshift32) for the test kit. The same seed gives the same sequence on every host, so a fault
// run that fails can be repeated exactly. A seed of 0 is replaced, because xorshift never leaves 0.
class Random {
public:
	explicit Random(uint32 seed) : state(seed != 0 ? seed : 0x9E3779B9u) {
	}


	uint32 Next() {
		state ^= state << 13;
		state ^= state >> 17;
		state ^= state << 5;
		return state;
	}


	// A value from 0 to bound - 1. Returns 0 when bound is 0.
	uint32 Below(uint32 bound) {
		return bound == 0 ? 0 : Next() % bound;
	}


	// True with the given chance in percent (0 never, 100 always).
	bool Chance(uint32 percent) {
		return Below(100) < percent;
	}


private:
	uint32 state;
};

}
