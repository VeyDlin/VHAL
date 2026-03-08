#pragma once
#include <Adapter/IAdapter.h>

#define VHAL_MCPWM_ADAPTER


class MCPWMAdapter : public IAdapter {
public:
	struct CountModeOption : IOption<uint32> { using IOption::IOption; };

	struct Parameters {
		uint32 resolutionHz = 1000000;
		uint32 periodTicks = 20000;
		CountModeOption countMode;
	};

	struct GeneratorConfig {
		int pin = -1;
	};

	struct DeadTimeConfig {
		uint32 posedgeDelayTicks = 0;
		uint32 negedgeDelayTicks = 0;
	};


public:
	std::function<void()> onBrake;


public:
	MCPWMAdapter() = default;
	MCPWMAdapter(int groupId) : groupId(groupId) { }

	virtual ResultStatus SetParameters(Parameters val) = 0;
	virtual ResultStatus AddComparator(uint32 compareValue = 0) = 0;
	virtual ResultStatus AddGenerator(GeneratorConfig config, uint8 comparatorIndex = 0) = 0;
	virtual ResultStatus SetCompare(uint8 comparatorIndex, uint32 value) = 0;
	virtual ResultStatus SetDeadTime(uint8 genA, uint8 genB, DeadTimeConfig config) = 0;
	virtual ResultStatus Start() = 0;
	virtual ResultStatus Stop() = 0;

	ResultStatus SetDutyPercent(uint8 comparatorIndex, float percent) {
		uint32 value = static_cast<uint32>((percent / 100.0f) * parameters.periodTicks);
		return SetCompare(comparatorIndex, value);
	}

	uint32 GetPeriodTicks() const {
		return parameters.periodTicks;
	}


protected:
	int groupId = 0;
	Parameters parameters;
};
