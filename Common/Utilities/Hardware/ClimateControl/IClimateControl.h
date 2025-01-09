#pragma once
#include <System/System.h>
#include <Adapter/OSAdapter/RTOS.h>
#include <Adapter/OSAdapter/Thread.h>
#include <chrono>
#include <functional>



class IClimateControl: public Thread<128> {
public:
	enum class WorkMode { Manual, Auto, Disable };
	enum class ControlMode { Heating, Cooling, HeatingCooling };


protected:
	WorkMode workMode = WorkMode::Disable;
	ControlMode controlMode = ControlMode::HeatingCooling;

	float holdTemperature = 0;

	std::chrono::milliseconds timeStep = 1s;

	bool waitHold = true;


public:
	std::function<float(float dirty)> onUpdateState;
	std::function<void()> onHold;


public:
	void SetHoldTemperature(float temperature) {
		holdTemperature = temperature;
	}

	float GetHoldTemperature() {
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
		timeStep = timeStep;
	}

	std::chrono::milliseconds GetTimeStep() {
		return timeStep;
	}



	void SetManualDirty(float dirty) {
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