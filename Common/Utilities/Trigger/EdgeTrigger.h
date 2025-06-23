#pragma once


// Used to detect rising edge of a condition.
// The condition triggers only once when it changes from false to true,
// and will not trigger again until it returns to false first.
//
// Example usage:
/*
   EdgeTrigger trigger;
   while (true) {
       bool condition = SomeErrorCheck();
       trigger = condition; // Pass the external condition each loop
       if (trigger) {
           // Executes only once when condition becomes true (rising edge).
           // Will not trigger again until condition becomes false, then true again.
       }
   }

   // Check current internal state:
   if (trigger == true) { } // Equivalent to trigger.GetState() == true
   if (trigger == false) { } // Equivalent to trigger.GetState() == false
*/
class EdgeTrigger {
private:
	bool lastState = false;
	bool currentState = false;

public:
	EdgeTrigger() { }

	EdgeTrigger(bool initial) :
		lastState(initial), currentState(initial) {
	}

	void operator=(bool condition) {
		lastState = currentState;
		currentState = condition;
	}

	operator bool() const {
		return currentState && !lastState;
	}

	bool operator!() const {
		return !currentState || lastState;
	}

	bool operator==(bool val) const {
		return currentState == val;
	}

	bool operator!=(bool val) const {
		return currentState != val;
	}

	bool GetState() const {
		return currentState;
	}
};
