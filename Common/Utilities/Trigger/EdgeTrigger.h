#pragma once
#include <Utilities/Trigger/Trigger.h>


// Rising-edge latch. Update() takes one sample: a false-to-true change since the previous sample sets the latch. Reading never has
// a side effect: IsSet() looks, and Take() clears. A pulse that rises and falls between two samples is not seen. Sample often
// enough for the signal, or call Trigger::Set() from the edge interrupt itself.
class EdgeTrigger {
private:
	Trigger latch;
	volatile bool level = false;


public:
	// Feeds one sample. Call it from one context only.
	void Update(bool sample) {
		if (sample && !level) {
			latch.Set();
		}
		level = sample;
	}


	// Returns true when a rising edge is latched, and clears it.
	[[nodiscard]] bool Take() {
		return latch.Take();
	}


	// Reads the latch without clearing it.
	bool IsSet() const {
		return latch.IsSet();
	}


	// The level of the last sample.
	bool GetState() const {
		return level;
	}
};
