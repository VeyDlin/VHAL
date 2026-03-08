#pragma once
#include "IClimateControl.h"


template <RealType Type = float>
class SwitchClimateControl: public IClimateControl<Type> {
	using Base = IClimateControl<Type>;
	using typename Base::WorkMode;
	using typename Base::ControlMode;
	using Base::timeStep;
	using Base::onUpdateState;
	using Base::workMode;
	using Base::controlMode;
	using Base::holdTemperature;
	using Base::waitHold;
	using Base::onHold;

public:
	Type temperatureTolerance = Type(0.5f);

public:
	SwitchClimateControl() { }


	virtual void Execute() override {
		Type dirty = 0;

		while(true) {
			this->Sleep(timeStep);

			if(onUpdateState == nullptr || workMode != WorkMode::Auto) {
				continue;
			}

			Type currentTemp = onUpdateState(dirty);
			dirty = GetDirtyFromTemperature(currentTemp);

			OnHold(currentTemp);
		}
	}


private:
	Type GetDirtyFromTemperature(Type currentTemp) {
		switch (controlMode) {
			case ControlMode::Heating:
				return currentTemp < holdTemperature ? Type(1) : Type(0);
			break;

			case ControlMode::Cooling:
				return currentTemp > holdTemperature ? Type(-1) : Type(0);
			break;

			case ControlMode::HeatingCooling:
				return currentTemp > holdTemperature ? Type(-1) : Type(1);
			break;
		}

		return Type(0);
	}


	void OnHold(Type currentTemp) {
		if(!waitHold) {
			return;
		}

		using std::abs;
		switch (controlMode) {
			case ControlMode::Heating:
				waitHold = currentTemp < holdTemperature;
			break;

			case ControlMode::Cooling:
				waitHold = currentTemp > holdTemperature;
			break;

			case ControlMode::HeatingCooling:
				waitHold = (abs(currentTemp - holdTemperature) > temperatureTolerance);
			break;
		}

		if(!waitHold && onHold) {
			onHold();
		}
	}
};
