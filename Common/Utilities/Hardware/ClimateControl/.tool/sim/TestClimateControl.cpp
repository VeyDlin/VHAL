#include <Utilities/Hardware/ClimateControl/PidClimateControl.h>
#include <Utilities/Hardware/ClimateControl/SwitchClimateControl.h>
#include <Utilities/Math/IQMath/IQMath.h>
#include <cmath>
#include <cstdio>


// Host tests for SwitchClimateControl and PidClimateControl with float, IQ<16> and IQ<24>. Each test feeds a scripted
// temperature to the control loop, one value per iteration, and checks the commands and the hold events by hand. The loop
// runs in Execute; the shim stops it after a fixed number of iterations.


namespace {


int failureCount = 0;


void ExpectNear(double actual, double expected, int line) {
	if (!(std::fabs(actual - expected) <= 1e-6)) {
		failureCount++;
		std::fprintf(stderr, "TestClimateControl.cpp:%d: value %.9f, expected %.9f\n", line, actual, expected);
	}
}


void ExpectTrue(bool condition, int line) {
	if (!condition) {
		failureCount++;
		std::fprintf(stderr, "TestClimateControl.cpp:%d: condition failed\n", line);
	}
}


// The modes are declared public in IClimateControl. The derived classes only re-export them privately.
template<RealType Type>
using WorkModeOf = typename IClimateControl<Type>::WorkMode;

template<RealType Type>
using ControlModeOf = typename IClimateControl<Type>::ControlMode;


// Runs the loop for count iterations. The shim throws after the budget is spent, and the throw ends the loop here.
template<typename Control>
void RunLoop(Control& control, uint32 count) {
	ClimateShim::tickBudget = count;
	try {
		control.Execute();
	} catch (ClimateTickStop&) {
	}
}


// Heating, hold 20. Temperatures 18, 19, 19.9, 20, 20.5. The switch commands 1 (heat) below the hold and 0 at or above it,
// and the hold event fires once, at the iteration where the temperature reaches 20.
template<RealType Type>
void SwitchHeating(const char* name) {
	const double temps[5] = { 18.0, 19.0, 19.9, 20.0, 20.5 };
	const double expectedArgs[5] = { 0.0, 1.0, 1.0, 1.0, 0.0 };

	SwitchClimateControl<Type> control;
	control.SetWorkMode(WorkModeOf<Type>::Auto);
	control.SetControlMode(ControlModeOf<Type>::Heating);
	control.SetHoldTemperature(Type(20));

	uint32 index = 0;
	uint32 holds = 0;
	control.onUpdateState = [&](Type dirty) -> Type {
		if (index < 5) {
			ExpectNear(static_cast<double>(static_cast<float>(dirty)), expectedArgs[index], __LINE__);
		}
		Type value = RealFrom<Type>(static_cast<float>(temps[index < 5 ? index : 4]));
		index++;
		return value;
	};
	control.onHold = [&holds]() {
		holds++;
	};

	RunLoop(control, 5);
	ExpectTrue(index == 5, __LINE__);
	ExpectTrue(holds == 1, __LINE__);
	std::printf("switch heating %-8s calls %u, holds %u\n", name, index, holds);
}


// Cooling, hold 20. Temperatures 22, 21, 20.4, 20, 19.6. The switch commands -1 (cool) above the hold and 0 at or below it.
template<RealType Type>
void SwitchCooling(const char* name) {
	const double temps[5] = { 22.0, 21.0, 20.4, 20.0, 19.6 };
	const double expectedArgs[5] = { 0.0, -1.0, -1.0, -1.0, 0.0 };

	SwitchClimateControl<Type> control;
	control.SetWorkMode(WorkModeOf<Type>::Auto);
	control.SetControlMode(ControlModeOf<Type>::Cooling);
	control.SetHoldTemperature(Type(20));

	uint32 index = 0;
	uint32 holds = 0;
	control.onUpdateState = [&](Type dirty) -> Type {
		if (index < 5) {
			ExpectNear(static_cast<double>(static_cast<float>(dirty)), expectedArgs[index], __LINE__);
		}
		Type value = RealFrom<Type>(static_cast<float>(temps[index < 5 ? index : 4]));
		index++;
		return value;
	};
	control.onHold = [&holds]() {
		holds++;
	};

	RunLoop(control, 5);
	ExpectTrue(index == 5, __LINE__);
	ExpectTrue(holds == 1, __LINE__);
	std::printf("switch cooling %-8s calls %u, holds %u\n", name, index, holds);
}


// Heating, hold 20, 18 degrees for three iterations. The error is 2, so the PID output saturates at its maximum of 1 and
// every iteration commands 1. The hold does not fire, since the temperature is below the hold window.
template<RealType Type>
void PidHeatingBelow(const char* name) {
	PidClimateControl<Type> control;
	control.SetWorkMode(WorkModeOf<Type>::Auto);
	control.SetControlMode(ControlModeOf<Type>::Heating);
	control.SetHoldTemperature(Type(20));

	uint32 commands = 0;
	uint32 holds = 0;
	control.onUpdateState = [&](Type dirty) -> Type {
		if (dirty != Type(0)) {
			commands++;
			ExpectNear(static_cast<double>(static_cast<float>(dirty)), 1.0, __LINE__);
		}
		return Type(18);
	};
	control.onHold = [&holds]() {
		holds++;
	};

	RunLoop(control, 3);
	ExpectTrue(commands == 3, __LINE__);
	ExpectTrue(holds == 0, __LINE__);
	std::printf("pid heating %-8s below hold: commands %u, holds %u\n", name, commands, holds);
}


// Heating, hold 20, 20 degrees for two iterations. The error is zero, so nothing is commanded and the hold fires once.
template<RealType Type>
void PidAtHold(const char* name) {
	PidClimateControl<Type> control;
	control.SetWorkMode(WorkModeOf<Type>::Auto);
	control.SetControlMode(ControlModeOf<Type>::Heating);
	control.SetHoldTemperature(Type(20));

	uint32 commands = 0;
	uint32 holds = 0;
	control.onUpdateState = [&](Type dirty) -> Type {
		if (dirty != Type(0)) {
			commands++;
		}
		return Type(20);
	};
	control.onHold = [&holds]() {
		holds++;
	};

	RunLoop(control, 2);
	ExpectTrue(commands == 0, __LINE__);
	ExpectTrue(holds == 1, __LINE__);
	std::printf("pid heating %-8s at hold: commands %u, holds %u\n", name, commands, holds);
}


template<RealType Type>
void RunAllFor(const char* name) {
	SwitchHeating<Type>(name);
	SwitchCooling<Type>(name);
	PidHeatingBelow<Type>(name);
	PidAtHold<Type>(name);
}


}


int main() {
	RunAllFor<float>("float");
	RunAllFor<IQ<16>>("IQ<16>");
	RunAllFor<IQ<20>>("IQ<20>");
	RunAllFor<IQ<24>>("IQ<24>");

	if (failureCount != 0) {
		std::fprintf(stderr, "%d check(s) failed\n", failureCount);
		return 1;
	}
	std::printf("TestClimateControl passed\n");
	return 0;
}
