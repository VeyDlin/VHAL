#pragma once
#include "IClimateControl.h"
#include <cmath>


class SwitchClimateControl: public IClimateControl {
public:
	float temperatureTolerance = 0.5f; // Temperature tolerance for HeatingCooling mode
	
public:
	SwitchClimateControl() { }


	virtual void Execute() override {
		float dirty = 0;

		while(true) {
			Sleep(timeStep);

			if(onUpdateState == nullptr || workMode != WorkMode::Auto) {
				continue;
			}

			float currentTemp = onUpdateState(dirty);
			dirty = GetDirtyFromTemperature(currentTemp);

			OnHold(currentTemp);
		}
	}


private:
	float GetDirtyFromTemperature(float currentTemp) {
		switch (controlMode) {
			case ControlMode::Heating:
				return currentTemp < holdTemperature ? 1 : 0;
			break;

			case ControlMode::Cooling:
				return currentTemp > holdTemperature ? -1 : 0;
			break;

			case ControlMode::HeatingCooling:
				return currentTemp > holdTemperature ? -1 : 1;
			break;
		}

		return 0;
	}


	void OnHold(float currentTemp) {
		if(!waitHold) {
			return;
		}

		switch (controlMode) {
			case ControlMode::Heating:
				waitHold = currentTemp < holdTemperature;
			break;

			case ControlMode::Cooling:
				waitHold = currentTemp > holdTemperature;
			break;

			case ControlMode::HeatingCooling:
				waitHold = (std::abs(currentTemp - holdTemperature) > temperatureTolerance);
			break;
		}

		if(!waitHold && onHold) {
			onHold();
		}
	}
};
