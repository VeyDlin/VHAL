#pragma once
#include <VHAL.h>
#include <chrono>

namespace Buttons {


class Button {
private:
	AGPIO& gpio;
	bool lastState = false;
	bool currentState = false;
	bool rawState = false;
	std::chrono::milliseconds lastChangeTime{0};
	std::chrono::milliseconds debounceTime{50};

public:
	Button(AGPIO& gpioAdapter)
		: gpio(gpioAdapter) { }

	void Update(std::chrono::milliseconds currentTime) {
		lastState = currentState;

		// GetState() already applies the AGPIO inversion, so the pin polarity is
		// declared once on the adapter and not repeated here.
		bool newRawState = gpio.GetState();

		if (newRawState != rawState) {
			rawState = newRawState;
			lastChangeTime = currentTime;
		}
		if (rawState != currentState && (currentTime - lastChangeTime) >= debounceTime) {
			currentState = rawState;
		}
	}

	void SetDebounceTime(std::chrono::milliseconds time) { debounceTime = time; }

	bool IsPressed() const { return currentState; }

	bool WasPressed() const { return !lastState && currentState; }

	bool WasReleased() const { return lastState && !currentState; }

	bool HasChanged() const { return lastState != currentState; }
};

}
