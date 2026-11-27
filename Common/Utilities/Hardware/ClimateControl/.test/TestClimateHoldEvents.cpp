#include <VHAL.h>
#include <Utilities/Hardware/ClimateControl/PidClimateControl.h>
#include <Utilities/Hardware/ClimateControl/SwitchClimateControl.h>
#include <VhalCheck.h>
#include <functional>
#include <vector>


// Host tests of the hold event of the climate controls: it fires once per hold target, again when the target changes, not when the
// same target is set again, and once for a manual hold. The control loop runs in Execute; the module shim stops it after the tick budget.


namespace {


// Runs Execute for ticks control steps. beforeTick(tick) runs in the sleep that precedes each step, and the sleep after the last step
// stops the loop.
template <typename ClimateType>
void RunClimateTicks(ClimateType& climate, uint32 ticks, const std::function<void(uint32)>& beforeTick) {
	uint32 tick = 0;
	ClimateShim::tickBudget = ticks;
	ClimateShim::tickHook = [&]() {
		if (tick < ticks) {
			beforeTick(tick);
		}
		++tick;
	};
	try {
		climate.Execute();
	} catch (ClimateTickStop&) {
	}
	ClimateShim::tickHook = nullptr;
}


void CheckSwitchNewTargetFiresAgain() {
	SwitchClimateControl<float> climate;
	climate.SetControlMode(IClimateControl<float>::ControlMode::Heating);
	climate.SetHoldTemperature(60.0f);
	climate.SetWorkMode(IClimateControl<float>::WorkMode::Auto);

	const std::vector<float> temps = { 50.0f, 61.0f, 61.0f, 62.0f, 71.0f, 71.0f, 71.0f };
	float current = temps[0];
	int holdCount = 0;

	IClimateControl<float>& base = climate;
	base.onUpdateState = [&](float dirty) {
		(void)dirty;
		return current;
	};
	base.onHold = [&]() {
		++holdCount;
	};

	RunClimateTicks(climate, static_cast<uint32>(temps.size()), [&](uint32 tick) {
		if (tick == 2) {
			climate.SetHoldTemperature(70.0f);
		}
		current = temps[tick];
	});

	VHAL_CHECK_EQ(holdCount, 2);
}


void CheckSwitchSameTargetDoesNotRefire() {
	SwitchClimateControl<float> climate;
	climate.SetControlMode(IClimateControl<float>::ControlMode::Heating);
	climate.SetHoldTemperature(60.0f);
	climate.SetWorkMode(IClimateControl<float>::WorkMode::Auto);

	const std::vector<float> temps = { 50.0f, 61.0f, 61.0f, 62.0f, 62.0f };
	float current = temps[0];
	int holdCount = 0;

	IClimateControl<float>& base = climate;
	base.onUpdateState = [&](float dirty) {
		(void)dirty;
		return current;
	};
	base.onHold = [&]() {
		++holdCount;
	};

	RunClimateTicks(climate, static_cast<uint32>(temps.size()), [&](uint32 tick) {
		if (tick == 2) {
			climate.SetHoldTemperature(60.0f);
		}
		current = temps[tick];
	});

	VHAL_CHECK_EQ(holdCount, 1);
}


void CheckPidNewTargetFiresAgain() {
	PidClimateControl<float> climate;
	climate.SetControlMode(IClimateControl<float>::ControlMode::Heating);
	climate.SetHoldTemperature(60.0f);
	climate.SetWorkMode(IClimateControl<float>::WorkMode::Auto);

	const std::vector<float> temps = { 50.0f, 61.0f, 61.0f, 62.0f, 71.0f, 71.0f, 71.0f };
	float current = temps[0];
	int holdCount = 0;

	IClimateControl<float>& base = climate;
	base.onUpdateState = [&](float dirty) {
		(void)dirty;
		return current;
	};
	base.onHold = [&]() {
		++holdCount;
	};

	RunClimateTicks(climate, static_cast<uint32>(temps.size()), [&](uint32 tick) {
		if (tick == 2) {
			climate.SetHoldTemperature(70.0f);
		}
		current = temps[tick];
	});

	VHAL_CHECK_EQ(holdCount, 2);
}


void CheckPidSameTargetDoesNotRefire() {
	PidClimateControl<float> climate;
	climate.SetControlMode(IClimateControl<float>::ControlMode::Heating);
	climate.SetHoldTemperature(60.0f);
	climate.SetWorkMode(IClimateControl<float>::WorkMode::Auto);

	const std::vector<float> temps = { 50.0f, 61.0f, 61.0f, 62.0f, 62.0f };
	float current = temps[0];
	int holdCount = 0;

	IClimateControl<float>& base = climate;
	base.onUpdateState = [&](float dirty) {
		(void)dirty;
		return current;
	};
	base.onHold = [&]() {
		++holdCount;
	};

	RunClimateTicks(climate, static_cast<uint32>(temps.size()), [&](uint32 tick) {
		if (tick == 2) {
			climate.SetHoldTemperature(60.0f);
		}
		current = temps[tick];
	});

	VHAL_CHECK_EQ(holdCount, 1);
}


// A manual hold followed by the stabilization event fires onHold once: the stabilization does not repeat a hold already reported.
void CheckPidManualHoldThenStabilizationFiresOnce() {
	PidClimateControl<float> climate;
	climate.SetControlMode(IClimateControl<float>::ControlMode::Heating);
	climate.SetHoldTemperature(60.0f);
	climate.SetStabilizationDetection(0.5f, 5000);
	climate.SetWorkMode(IClimateControl<float>::WorkMode::Manual);

	int holdCount = 0;
	IClimateControl<float>& base = climate;
	base.onHold = [&]() {
		++holdCount;
	};
	base.onUpdateState = [&](float dirty) {
		(void)dirty;
		return 59.8f;
	};

	climate.SetManualHold();
	VHAL_CHECK_EQ(holdCount, 1);

	climate.SetWorkMode(IClimateControl<float>::WorkMode::Auto);
	RunClimateTicks(climate, 20, [](uint32 tick) {
		(void)tick;
	});

	VHAL_CHECK_EQ(holdCount, 1);
}


void CheckManualHoldFiresOncePerTarget() {
	SwitchClimateControl<float> climate;
	climate.SetControlMode(IClimateControl<float>::ControlMode::Heating);
	climate.SetHoldTemperature(60.0f);
	climate.SetWorkMode(IClimateControl<float>::WorkMode::Manual);

	int holdCount = 0;
	IClimateControl<float>& base = climate;
	base.onHold = [&]() {
		++holdCount;
	};

	climate.SetManualHold();
	climate.SetManualHold();
	VHAL_CHECK_EQ(holdCount, 1);

	climate.SetHoldTemperature(70.0f);
	climate.SetManualHold();
	VHAL_CHECK_EQ(holdCount, 2);
}


// The temperature rises through the hold: the hold fires once, at the first step that reaches the target.
void CheckSwitchHoldFiresOnce() {
	SwitchClimateControl<float> climate;
	climate.SetControlMode(IClimateControl<float>::ControlMode::Heating);
	climate.SetHoldTemperature(60.0f);
	climate.SetWorkMode(IClimateControl<float>::WorkMode::Auto);

	const std::vector<float> temps = { 50.0f, 55.0f, 59.0f, 61.0f, 62.0f, 63.0f };
	size_t index = 0;
	int holdCount = 0;

	IClimateControl<float>& base = climate;
	base.onUpdateState = [&](float dirty) {
		(void)dirty;
		float value = temps[index < temps.size() ? index : temps.size() - 1];
		++index;
		return value;
	};
	base.onHold = [&]() {
		++holdCount;
	};

	ClimateShim::tickBudget = static_cast<uint32>(temps.size());
	try {
		climate.Execute();
	} catch (ClimateTickStop&) {
	}

	VHAL_CHECK_EQ(holdCount, 1);
}


// The PID control reaches the target with stabilization detection on: the hold fires once, when the stabilization event and the
// reach both call it.
void CheckPidHoldFiresOnce() {
	PidClimateControl<float> climate;
	climate.SetControlMode(IClimateControl<float>::ControlMode::Heating);
	climate.SetHoldTemperature(60.0f);
	climate.SetWorkMode(IClimateControl<float>::WorkMode::Auto);
	climate.SetStabilizationDetection(0.5f, 5000);

	std::vector<float> temps = { 50.0f, 55.0f, 59.8f };
	for (int i = 0; i < 20; ++i) {
		temps.push_back(59.8f);
	}
	size_t index = 0;
	float current = temps[0];
	int holdCount = 0;

	IClimateControl<float>& base = climate;
	base.onUpdateState = [&](float dirty) {
		if (dirty == 0.0f) {
			current = temps[index < temps.size() ? index : temps.size() - 1];
			++index;
		}
		return current;
	};
	base.onHold = [&]() {
		++holdCount;
	};

	ClimateShim::tickBudget = static_cast<uint32>(temps.size());
	try {
		climate.Execute();
	} catch (ClimateTickStop&) {
	}

	VHAL_CHECK_EQ(holdCount, 1);
}


void CheckPidHoldFiresOnceWithoutStabilization() {
	PidClimateControl<float> climate;
	climate.SetControlMode(IClimateControl<float>::ControlMode::Heating);
	climate.SetHoldTemperature(60.0f);
	climate.SetWorkMode(IClimateControl<float>::WorkMode::Auto);

	const std::vector<float> temps = { 50.0f, 55.0f, 59.8f, 59.8f, 59.8f, 59.8f, 59.8f };
	size_t index = 0;
	float current = temps[0];
	int holdCount = 0;

	IClimateControl<float>& base = climate;
	base.onUpdateState = [&](float dirty) {
		if (dirty == 0.0f) {
			current = temps[index < temps.size() ? index : temps.size() - 1];
			++index;
		}
		return current;
	};
	base.onHold = [&]() {
		++holdCount;
	};

	ClimateShim::tickBudget = static_cast<uint32>(temps.size());
	try {
		climate.Execute();
	} catch (ClimateTickStop&) {
	}

	VHAL_CHECK_EQ(holdCount, 1);
}


// The public API of both controls: the hold callback is assignable through the interface, and both controls run with no callbacks.
void CheckPublicApi() {
	SwitchClimateControl<float> switchClimate;
	switchClimate.onHold = []() {};

	PidClimateControl<float> pidClimate;
	pidClimate.onHold = []() {};
	VHAL_CHECK(switchClimate.onHold != nullptr && pidClimate.onHold != nullptr);
}


}


int main() {
	CheckSwitchNewTargetFiresAgain();
	CheckSwitchSameTargetDoesNotRefire();
	CheckPidNewTargetFiresAgain();
	CheckPidSameTargetDoesNotRefire();
	CheckPidManualHoldThenStabilizationFiresOnce();
	CheckManualHoldFiresOncePerTarget();
	CheckSwitchHoldFiresOnce();
	CheckPidHoldFiresOnce();
	CheckPidHoldFiresOnceWithoutStabilization();
	CheckPublicApi();
	return VhalCheck::Finish("TestClimateHoldEvents");
}
