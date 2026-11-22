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
		bool invert = false;
	};


public:
	std::function<void(ChannelOption channel)> onFadeEnd;


public:
	LEDCAdapter() = default;
	LEDCAdapter(TimerOption timer, SpeedModeOption speedMode)
		: timer(timer), speedMode(speedMode) { }

	virtual ResultStatus SetParameters(Parameters val) = 0;
	virtual ResultStatus AddChannel(ChannelConfig config) = 0;
	virtual ResultStatus SetDuty(ChannelOption channel, uint32 duty) = 0;
	virtual ResultStatus SetFrequency(uint32 frequencyHz) = 0;
	virtual uint32 GetFrequency() = 0;
	virtual uint32 GetDuty(ChannelOption channel) = 0;
	virtual ResultStatus FadeTo(ChannelOption channel, uint32 targetDuty, uint32 fadeTimeMs) = 0;
	virtual ResultStatus Stop(ChannelOption channel, uint32 idleLevel = 0) = 0;

	uint32 GetMaxDuty() const {
		return (1u << parameters.dutyResolution.Get());
	}

	ResultStatus SetDutyPercent(ChannelOption channel, float percent) {
		uint32 duty = static_cast<uint32>((percent / 100.0f) * GetMaxDuty());
		return SetDuty(channel, duty);
	}


	// Replaces onFadeEnd. See "Changing a callback while its interrupt may run" in the wiki for when this is safe on ESP32.
	// The port then runs FadeEndHandlerChanged() outside the section, so the driver learns about the new handler.
	void SetOnFadeEnd(std::function<void(ChannelOption channel)> handler) {
		std::function<void(ChannelOption channel)> old;
		System::CriticalSection(true);
		old = std::move(onFadeEnd);
		onFadeEnd = std::move(handler);
		System::CriticalSection(false);
		FadeEndHandlerChanged();
	}


protected:
	TimerOption timer;
	SpeedModeOption speedMode;
	Parameters parameters;


	// Called after SetOnFadeEnd() has replaced the handler, with interrupts enabled. A port overrides it when its driver
	// needs a registration for the handler to run.
	virtual void FadeEndHandlerChanged() { }
};
