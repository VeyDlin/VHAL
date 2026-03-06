#pragma once
#include <Adapter/IAdapter.h>

#define VHAL_LEDC_ADAPTER


class LEDCAdapter : public IAdapter {
public:
	struct TimerOption : IOption<uint32> { using IOption::IOption; };
	struct SpeedModeOption : IOption<uint32> { using IOption::IOption; };
	struct ChannelOption : IOption<uint32> { using IOption::IOption; };
	struct ResolutionOption : IOption<uint32> { using IOption::IOption; };

	struct Parameters {
		uint32 frequencyHz = 5000;
		ResolutionOption dutyResolution;
	};

	struct ChannelConfig {
		ChannelOption channel;
		int pin = -1;
		uint32 duty = 0;
		uint32 hpoint = 0;
	};


public:
	std::function<void(ChannelOption channel)> onFadeEnd;


public:
	LEDCAdapter() = default;
	LEDCAdapter(TimerOption timer, SpeedModeOption speedMode)
		: timer(timer), speedMode(speedMode) { }

	virtual Status::statusType SetParameters(Parameters val) = 0;
	virtual Status::statusType AddChannel(ChannelConfig config) = 0;
	virtual Status::statusType SetDuty(ChannelOption channel, uint32 duty) = 0;
	virtual Status::statusType SetFrequency(uint32 frequencyHz) = 0;
	virtual uint32 GetFrequency() = 0;
	virtual uint32 GetDuty(ChannelOption channel) = 0;
	virtual Status::statusType FadeTo(ChannelOption channel, uint32 targetDuty, uint32 fadeTimeMs) = 0;
	virtual Status::statusType Stop(ChannelOption channel, uint32 idleLevel = 0) = 0;

	uint32 GetMaxDuty() const {
		return (1u << parameters.dutyResolution.Get());
	}

	Status::statusType SetDutyPercent(ChannelOption channel, float percent) {
		uint32 duty = static_cast<uint32>((percent / 100.0f) * GetMaxDuty());
		return SetDuty(channel, duty);
	}


protected:
	TimerOption timer;
	SpeedModeOption speedMode;
	Parameters parameters;
};
