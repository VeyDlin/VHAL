#pragma once


// Rising-edge detector: fires once on false-to-true transition
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
