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
#if SOC_LEDC_SUPPORT_HS_MODE
		static inline constexpr SpeedModeOption High { LEDC_HIGH_SPEED_MODE };
#endif
	};

	struct Channel {
		static inline constexpr ChannelOption C0 { LEDC_CHANNEL_0 };
		static inline constexpr ChannelOption C1 { LEDC_CHANNEL_1 };
		static inline constexpr ChannelOption C2 { LEDC_CHANNEL_2 };
		static inline constexpr ChannelOption C3 { LEDC_CHANNEL_3 };
		static inline constexpr ChannelOption C4 { LEDC_CHANNEL_4 };
		static inline constexpr ChannelOption C5 { LEDC_CHANNEL_5 };
#if SOC_LEDC_CHANNEL_NUM > 6
		static inline constexpr ChannelOption C6 { LEDC_CHANNEL_6 };
		static inline constexpr ChannelOption C7 { LEDC_CHANNEL_7 };
#endif
	};

	struct Resolution {
		static inline constexpr ResolutionOption B1  { LEDC_TIMER_1_BIT };
		static inline constexpr ResolutionOption B2  { LEDC_TIMER_2_BIT };
		static inline constexpr ResolutionOption B3  { LEDC_TIMER_3_BIT };
		static inline constexpr ResolutionOption B4  { LEDC_TIMER_4_BIT };
		static inline constexpr ResolutionOption B5  { LEDC_TIMER_5_BIT };
		static inline constexpr ResolutionOption B6  { LEDC_TIMER_6_BIT };
		static inline constexpr ResolutionOption B7  { LEDC_TIMER_7_BIT };
		static inline constexpr ResolutionOption B8  { LEDC_TIMER_8_BIT };
		static inline constexpr ResolutionOption B9  { LEDC_TIMER_9_BIT };
		static inline constexpr ResolutionOption B10 { LEDC_TIMER_10_BIT };
		static inline constexpr ResolutionOption B11 { LEDC_TIMER_11_BIT };
		static inline constexpr ResolutionOption B12 { LEDC_TIMER_12_BIT };
		static inline constexpr ResolutionOption B13 { LEDC_TIMER_13_BIT };
		static inline constexpr ResolutionOption B14 { LEDC_TIMER_14_BIT };
#if SOC_LEDC_TIMER_BIT_WIDTH > 14
		static inline constexpr ResolutionOption B15 { LEDC_TIMER_15_BIT };
		static inline constexpr ResolutionOption B16 { LEDC_TIMER_16_BIT };
		static inline constexpr ResolutionOption B17 { LEDC_TIMER_17_BIT };
		static inline constexpr ResolutionOption B18 { LEDC_TIMER_18_BIT };
		static inline constexpr ResolutionOption B19 { LEDC_TIMER_19_BIT };
		static inline constexpr ResolutionOption B20 { LEDC_TIMER_20_BIT };
#endif
	};


public:
	LEDCAdapterESP() = default;

	LEDCAdapterESP(TimerOption timer, SpeedModeOption speedMode)
		: LEDCAdapter(timer, speedMode) { }

	~LEDCAdapterESP() {
		// The driver keeps a pointer to this object for the fade-end callback; clear it before the object goes away.
		ledc_cbs_t noCallbacks = {};
		for (uint8 i = 0; i < channelCount; i++) {
			if (fadeCallbackRegistered[i]) {
				ledc_cb_register(
					static_cast<ledc_mode_t>(speedMode.Get()),
					static_cast<ledc_channel_t>(channels[i].channel.Get()),
					&noCallbacks,
					nullptr
				);
			}
		}

		for (uint8 i = 0; i < channelCount; i++) {
			ledc_stop(
				static_cast<ledc_mode_t>(speedMode.Get()),
				static_cast<ledc_channel_t>(channels[i].channel.Get()),
				0
			);
		}
	}


	ResultStatus SetParameters(Parameters val) override {
		parameters = val;
		channelCount = 0;
		for (uint8 i = 0; i < maxChannels; i++) {
			fadeCallbackRegistered[i] = false;
		}
		return Initialization();
	}


	ResultStatus AddChannel(ChannelConfig config) override {
		int slot = FindChannelSlot(config.channel.Get());
		bool appending = (slot < 0);
		if (appending) {
			if (channelCount >= maxChannels) {
				return ResultStatus::error;
			}
			slot = channelCount;
		}

		ledc_channel_config_t ch_config = {};
		ch_config.speed_mode = static_cast<ledc_mode_t>(speedMode.Get());
		ch_config.channel = static_cast<ledc_channel_t>(config.channel.Get());
		ch_config.timer_sel = static_cast<ledc_timer_t>(timer.Get());
		ch_config.intr_type = LEDC_INTR_DISABLE;
		ch_config.gpio_num = config.pin;
		ch_config.duty = config.duty;
		ch_config.hpoint = config.hpoint;
		ch_config.flags.output_invert = config.invert ? 1 : 0;

		if (ledc_channel_config(&ch_config) != ESP_OK) {
			return ResultStatus::error;
		}

		channels[slot] = config;
		if (appending) {
			channelCount++;
		}
		return ResultStatus::ok;
	}


	ResultStatus SetDuty(ChannelOption channel, uint32 duty) override {
		int slot = FindChannelSlot(channel.Get());
		uint32 hpoint = (slot >= 0) ? channels[slot].hpoint : 0;
		ledc_mode_t sm = static_cast<ledc_mode_t>(speedMode.Get());
		ledc_channel_t ch = static_cast<ledc_channel_t>(channel.Get());

		esp_err_t result = ESP_FAIL;
		if (fadeInstalled) {
			result = ledc_set_duty_and_update(sm, ch, duty, hpoint);
		} else {
			result = ledc_set_duty_with_hpoint(sm, ch, duty, hpoint);
			if (result == ESP_OK) {
				result = ledc_update_duty(sm, ch);
			}
		}
		return (result == ESP_OK) ? ResultStatus::ok : ResultStatus::error;
	}


	ResultStatus SetFrequency(uint32 frequencyHz) override {
		parameters.frequencyHz = frequencyHz;
		return (ledc_set_freq(
			static_cast<ledc_mode_t>(speedMode.Get()),
			static_cast<ledc_timer_t>(timer.Get()),
			frequencyHz
		) == ESP_OK) ? ResultStatus::ok : ResultStatus::error;
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


	ResultStatus FadeTo(ChannelOption channel, uint32 targetDuty, uint32 fadeTimeMs) override {
		if (EnsureFadeService() != ResultStatus::ok) {
			return ResultStatus::error;
		}

		ledc_mode_t sm = static_cast<ledc_mode_t>(speedMode.Get());
		ledc_channel_t ch = static_cast<ledc_channel_t>(channel.Get());

		// The LEDC fade interrupt stays enabled while flash is being written, so the callback is registered only
		// when there is a handler to call: an adapter without onFadeEnd never runs flash code from that interrupt.
		int slot = FindChannelSlot(channel.Get());
		if (slot >= 0 && onFadeEnd && !fadeCallbackRegistered[slot]) {
			ledc_cbs_t callbacks = {};
			callbacks.fade_cb = FadeCallback;
			if (ledc_cb_register(sm, ch, &callbacks, this) != ESP_OK) {
				return ResultStatus::error;
			}
			fadeCallbackRegistered[slot] = true;
		}

		if (ledc_set_fade_with_time(sm, ch, targetDuty, fadeTimeMs) != ESP_OK) {
			return ResultStatus::error;
		}
		return (ledc_fade_start(sm, ch, LEDC_FADE_NO_WAIT) == ESP_OK)
			? ResultStatus::ok : ResultStatus::error;
	}


	ResultStatus Stop(ChannelOption channel, uint32 idleLevel = 0) override {
		return (ledc_stop(
			static_cast<ledc_mode_t>(speedMode.Get()),
			static_cast<ledc_channel_t>(channel.Get()),
			idleLevel
		) == ESP_OK) ? ResultStatus::ok : ResultStatus::error;
	}


protected:
	static constexpr uint8 maxChannels = 8;

	ChannelConfig channels[maxChannels] = {};
	uint8 channelCount = 0;
	bool fadeCallbackRegistered[maxChannels] = {};
	static inline bool fadeInstalled = false;

	ResultStatus Initialization() override {
		auto status = BeforeInitialization();
		if (status != ResultStatus::ok) {
			return status;
		}

		ledc_timer_config_t timer_config = {};
		timer_config.speed_mode = static_cast<ledc_mode_t>(speedMode.Get());
		timer_config.timer_num = static_cast<ledc_timer_t>(timer.Get());
		timer_config.duty_resolution = static_cast<ledc_timer_bit_t>(parameters.dutyResolution.Get());
		timer_config.freq_hz = parameters.frequencyHz;
		timer_config.clk_cfg = LEDC_AUTO_CLK;

		if (ledc_timer_config(&timer_config) != ESP_OK) {
			return ResultStatus::error;
		}

		return AfterInitialization();
	}


private:
	ResultStatus EnsureFadeService() {
		if (fadeInstalled) {
			return ResultStatus::ok;
		}

		esp_err_t result = ledc_fade_func_install(0);
		if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
			return ResultStatus::error;
		}
		fadeInstalled = true;
		return ResultStatus::ok;
	}


	int FindChannelSlot(uint32 channelId) const {
		for (uint8 i = 0; i < channelCount; i++) {
			if (channels[i].channel.Get() == channelId) {
				return i;
			}
		}
		return -1;
	}


	// Deliberately not IRAM_ATTR: it calls onFadeEnd, which lives in flash anyway, and an in-class function placed
	// in IRAM fails to link on Xtensa as soon as it needs a literal that the linker cannot relax away.
	static bool FadeCallback(const ledc_cb_param_t *parameter, void *argument) {
		if (parameter->event == LEDC_FADE_END_EVT) {
			LEDCAdapterESP *self = static_cast<LEDCAdapterESP *>(argument);
			if (self->onFadeEnd) {
				self->onFadeEnd(ChannelOption(parameter->channel));
			}
		}
		return false;
	}
};
