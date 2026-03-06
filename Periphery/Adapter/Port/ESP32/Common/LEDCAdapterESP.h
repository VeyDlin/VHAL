#pragma once
#include <Adapter/Port/ESP32/Adapter/LEDCAdapter.h>


using ALEDC = class LEDCAdapterESP;


class LEDCAdapterESP : public LEDCAdapter {
public:
	struct Timer {
		static inline constexpr TimerOption T0 { LEDC_TIMER_0 };
		static inline constexpr TimerOption T1 { LEDC_TIMER_1 };
		static inline constexpr TimerOption T2 { LEDC_TIMER_2 };
		static inline constexpr TimerOption T3 { LEDC_TIMER_3 };
	};

	struct SpeedMode {
		static inline constexpr SpeedModeOption Low { LEDC_LOW_SPEED_MODE };
	};

	struct Channel {
		static inline constexpr ChannelOption C0 { LEDC_CHANNEL_0 };
		static inline constexpr ChannelOption C1 { LEDC_CHANNEL_1 };
		static inline constexpr ChannelOption C2 { LEDC_CHANNEL_2 };
		static inline constexpr ChannelOption C3 { LEDC_CHANNEL_3 };
		static inline constexpr ChannelOption C4 { LEDC_CHANNEL_4 };
		static inline constexpr ChannelOption C5 { LEDC_CHANNEL_5 };
	};

	struct Resolution {
		static inline constexpr ResolutionOption B8  { LEDC_TIMER_8_BIT };
		static inline constexpr ResolutionOption B10 { LEDC_TIMER_10_BIT };
		static inline constexpr ResolutionOption B12 { LEDC_TIMER_12_BIT };
		static inline constexpr ResolutionOption B13 { LEDC_TIMER_13_BIT };
		static inline constexpr ResolutionOption B14 { LEDC_TIMER_14_BIT };
	};


public:
	LEDCAdapterESP() = default;

	LEDCAdapterESP(TimerOption timer, SpeedModeOption speedMode)
		: LEDCAdapter(timer, speedMode) { }

	~LEDCAdapterESP() {
		for (uint8 i = 0; i < channelCount; i++) {
			ledc_stop(
				static_cast<ledc_mode_t>(speedMode.Get()),
				static_cast<ledc_channel_t>(channels[i].channel.Get()),
				0
			);
		}
		if (fadeInstalled) {
			ledc_fade_func_uninstall();
		}
	}


	Status::statusType SetParameters(Parameters val) override {
		parameters = val;
		channelCount = 0;
		return Initialization();
	}


	Status::statusType AddChannel(ChannelConfig config) override {
		if (channelCount >= maxChannels) {
			return Status::error;
		}

		ledc_channel_config_t ch_config = {};
		ch_config.speed_mode = static_cast<ledc_mode_t>(speedMode.Get());
		ch_config.channel = static_cast<ledc_channel_t>(config.channel.Get());
		ch_config.timer_sel = static_cast<ledc_timer_t>(timer.Get());
		ch_config.intr_type = LEDC_INTR_DISABLE;
		ch_config.gpio_num = config.pin;
		ch_config.duty = config.duty;
		ch_config.hpoint = config.hpoint;

		if (ledc_channel_config(&ch_config) != ESP_OK) {
			return Status::error;
		}

		channels[channelCount++] = config;
		return Status::ok;
	}


	Status::statusType SetDuty(ChannelOption channel, uint32 duty) override {
		return (ledc_set_duty_and_update(
			static_cast<ledc_mode_t>(speedMode.Get()),
			static_cast<ledc_channel_t>(channel.Get()),
			duty, 0
		) == ESP_OK) ? Status::ok : Status::error;
	}


	Status::statusType SetFrequency(uint32 frequencyHz) override {
		parameters.frequencyHz = frequencyHz;
		return (ledc_set_freq(
			static_cast<ledc_mode_t>(speedMode.Get()),
			static_cast<ledc_timer_t>(timer.Get()),
			frequencyHz
		) == ESP_OK) ? Status::ok : Status::error;
	}


	uint32 GetFrequency() override {
		return ledc_get_freq(
			static_cast<ledc_mode_t>(speedMode.Get()),
			static_cast<ledc_timer_t>(timer.Get())
		);
	}


	uint32 GetDuty(ChannelOption channel) override {
		return ledc_get_duty(
			static_cast<ledc_mode_t>(speedMode.Get()),
			static_cast<ledc_channel_t>(channel.Get())
		);
	}


	Status::statusType FadeTo(ChannelOption channel, uint32 targetDuty, uint32 fadeTimeMs) override {
		if (!fadeInstalled) {
			if (ledc_fade_func_install(0) != ESP_OK) {
				return Status::error;
			}
			fadeInstalled = true;
		}

		auto sm = static_cast<ledc_mode_t>(speedMode.Get());
		auto ch = static_cast<ledc_channel_t>(channel.Get());

		if (ledc_set_fade_with_time(sm, ch, targetDuty, fadeTimeMs) != ESP_OK) {
			return Status::error;
		}
		return (ledc_fade_start(sm, ch, LEDC_FADE_NO_WAIT) == ESP_OK)
			? Status::ok : Status::error;
	}


	Status::statusType Stop(ChannelOption channel, uint32 idleLevel = 0) override {
		return (ledc_stop(
			static_cast<ledc_mode_t>(speedMode.Get()),
			static_cast<ledc_channel_t>(channel.Get()),
			idleLevel
		) == ESP_OK) ? Status::ok : Status::error;
	}


protected:
	static constexpr uint8 maxChannels = 8;

	ChannelConfig channels[maxChannels] = {};
	uint8 channelCount = 0;
	bool fadeInstalled = false;

	Status::statusType Initialization() override {
		auto status = BeforeInitialization();
		if (status != Status::ok) {
			return status;
		}

		ledc_timer_config_t timer_config = {};
		timer_config.speed_mode = static_cast<ledc_mode_t>(speedMode.Get());
		timer_config.timer_num = static_cast<ledc_timer_t>(timer.Get());
		timer_config.duty_resolution = static_cast<ledc_timer_bit_t>(parameters.dutyResolution.Get());
		timer_config.freq_hz = parameters.frequencyHz;
		timer_config.clk_cfg = LEDC_AUTO_CLK;

		if (ledc_timer_config(&timer_config) != ESP_OK) {
			return Status::error;
		}

		return AfterInitialization();
	}
};
