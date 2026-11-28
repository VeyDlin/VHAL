#pragma once
#include <Adapter/GPIOAdapter.h>

// Host stand-in for the umbrella header VHAL.h. AGPIO derives from the real GPIOAdapter, so inversion and the logical
// GetState and SetState behave as they do on the target. Only the raw pin level is simulated: a test drives it with SetRawLevel.
class AGPIO: public GPIOAdapter<void> {
private:
	bool rawLevel = false;

public:
	AGPIO(uint8 gpioPin, bool gpioInversion = false)
		: GPIOAdapter<void>(gpioPin, gpioInversion) { }

	void SetRawLevel(bool level) {
		rawLevel = level;
	}

protected:
	ResultStatus Initialization() override {
		return ResultStatus::ok;
	}

	bool GetPinState() override {
		return rawLevel;
	}

	void SetPinState(bool state) override {
		rawLevel = state;
	}

	void TogglePinState() override {
		rawLevel = !rawLevel;
	}
};
