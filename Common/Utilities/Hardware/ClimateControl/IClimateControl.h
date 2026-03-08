#pragma once
#define VHAL_RTOS
#include <VHAL.h>
#include <chrono>
#include <functional>
#include <Utilities/Math/IQMath/IQ.h>



template <RealType Type = float>
class IClimateControl: public Thread<128> {
public:
	enum class WorkMode { Manual, Auto, Disable };
	enum class ControlMode { Heating, Cooling, HeatingCooling };


protected:
	WorkMode workMode = WorkMode::Disable;
	ControlMode controlMode = ControlMode::HeatingCooling;

	Type holdTemperature = 0;

	std::chrono::milliseconds timeStep = 1s;

	bool waitHold = true;


public:
	std::function<Type(Type dirty)> onUpdateState;
	std::function<void()> onHold;


public:
	void Start() {
		RTOS::CreateThread(*this);
	}


	void SetHoldTemperature(Type temperature) {
		holdTemperature = temperature;
	}


	Type GetHoldTemperature() {
		return holdTemperature;
	}


	void SetWorkMode(WorkMode mode) {
		workMode = mode;
	}


	WorkMode GetWorkMode() {
		return workMode;
	}


	void SetControlMode(ControlMode mode) {
		controlMode = mode;
	}


	ControlMode GetControlMode() {
		return controlMode;
	}


	void SetTimeStep(std::chrono::milliseconds timeStep) {
		this->timeStep = timeStep;
	}


	std::chrono::milliseconds GetTimeStep() {
		return timeStep;
	}


	void SetManualDirty(Type dirty) {
		if(workMode == WorkMode::Manual && onUpdateState != nullptr) {
			onUpdateState(dirty);
		}
	}


	void SetManualHold() {
		if(workMode == WorkMode::Manual && onHold != nullptr && waitHold) {
			waitHold = false;
			onHold();
		}
	}
};
