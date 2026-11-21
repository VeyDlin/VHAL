#pragma once
#include <Adapter/TIMAdapter.h>


// TIM (general purpose and advanced timer) adapter for STM32F4. It covers every timer the chip defines. TIM8 is absent on
// F401 and F411, where TIM1 is the only advanced timer. TIM8 exists on F407 and F446 (and on other F4 chips that define
// TIM8_BASE).
//
// The BSP must do the following before the first Initialization():
//   - Enable the bus clock of each timer it uses: LL_APB2_GRP1_PERIPH_TIM1, TIM8, TIM9, TIM10, TIM11 on APB2, and
//     LL_APB1_GRP1_PERIPH_TIM2 to TIM7, TIM12, TIM13, TIM14 on APB1. Without the clock, register writes are dropped.
//   - Route the timer NVIC vectors to IrqHandler() of the adapter that owns each timer. Some vectors serve two timers:
//     TIM1_BRK_TIM9, TIM1_UP_TIM10, TIM1_TRG_COM_TIM11, TIM8_BRK_TIM12, TIM8_UP_TIM13 and TIM8_TRG_COM_TIM14. Forward the
//     vector to the IrqHandler() of every adapter on it. Each IrqHandler() reads only the flags of its own timer, so the
//     adapters ignore each other's events. TIM1_CC and TIM8_CC serve one timer each. TIM2 to TIM5 have one vector each.
//   - The adapter never touches NVIC: it does not set priorities and does not enable or disable vectors.


using ATIM = class TIMAdapterF4;


class TIMAdapterF4: public TIMAdapter<TIM_TypeDef> {
public:
	struct ClockDivision {
		static inline constexpr ClockDivisionOption D1 { LL_TIM_CLOCKDIVISION_DIV1 };
		static inline constexpr ClockDivisionOption D2 { LL_TIM_CLOCKDIVISION_DIV2 };
		static inline constexpr ClockDivisionOption D4 { LL_TIM_CLOCKDIVISION_DIV4 };
	};

	struct Counter {
		static inline constexpr CounterOption Up           { LL_TIM_COUNTERMODE_UP };
		static inline constexpr CounterOption Down         { LL_TIM_COUNTERMODE_DOWN };
		static inline constexpr CounterOption CenterUp     { LL_TIM_COUNTERMODE_CENTER_UP };
		static inline constexpr CounterOption CenterDown   { LL_TIM_COUNTERMODE_CENTER_DOWN };
		static inline constexpr CounterOption CenterUpDown { LL_TIM_COUNTERMODE_CENTER_UP_DOWN };
	};

	struct OutputCompare {
		static inline constexpr OutputCompareOption Frozen         { LL_TIM_OCMODE_FROZEN };
		// Deprecated alias: the name was misspelled before. Kept so existing callers still build; use Frozen.
		[[deprecated("Use Frozen")]] static inline constexpr OutputCompareOption Forzen { LL_TIM_OCMODE_FROZEN };
		static inline constexpr OutputCompareOption Active         { LL_TIM_OCMODE_ACTIVE };
		static inline constexpr OutputCompareOption Inactive       { LL_TIM_OCMODE_INACTIVE };
		static inline constexpr OutputCompareOption Toggle         { LL_TIM_OCMODE_TOGGLE };
		static inline constexpr OutputCompareOption ForcedInactive { LL_TIM_OCMODE_FORCED_INACTIVE };
		static inline constexpr OutputCompareOption ForcedActive   { LL_TIM_OCMODE_FORCED_ACTIVE };
		static inline constexpr OutputCompareOption Pwm            { LL_TIM_OCMODE_PWM1 };
		static inline constexpr OutputCompareOption Pwm2           { LL_TIM_OCMODE_PWM2 };
	};

	struct Channel {
		static inline constexpr ChannelOption C1 { LL_TIM_CHANNEL_CH1, LL_TIM_CHANNEL_CH1N };
		static inline constexpr ChannelOption C2 { LL_TIM_CHANNEL_CH2, LL_TIM_CHANNEL_CH2N };
		static inline constexpr ChannelOption C3 { LL_TIM_CHANNEL_CH3, LL_TIM_CHANNEL_CH3N };
		static inline constexpr ChannelOption C4 { LL_TIM_CHANNEL_CH4, 0 };
	};

	struct InputPrescaler {
		static inline constexpr InputPrescalerOption Div1 { LL_TIM_ICPSC_DIV1 };
		static inline constexpr InputPrescalerOption Div2 { LL_TIM_ICPSC_DIV2 };
		static inline constexpr InputPrescalerOption Div4 { LL_TIM_ICPSC_DIV4 };
		static inline constexpr InputPrescalerOption Div8 { LL_TIM_ICPSC_DIV8 };
	};

	struct Interrupt {
		static inline constexpr InterruptOption CaptureCompare1 { 1 };
		static inline constexpr InterruptOption CaptureCompare2 { 2 };
		static inline constexpr InterruptOption CaptureCompare3 { 3 };
		static inline constexpr InterruptOption CaptureCompare4 { 4 };
		static inline constexpr InterruptOption Update          { 5 };
		static inline constexpr InterruptOption Break           { 6 };
		static inline constexpr InterruptOption Trigger         { 7 };
		static inline constexpr InterruptOption Commutation     { 8 };
	};


public:
	TIMAdapterF4() { }


	TIMAdapterF4(TIM_TypeDef *timer, uint32 busClockHz, Bitness bit = Bitness::B16): TIMAdapter(timer, busClockHz, bit) { }


	virtual inline void IrqHandler() override {

		// Capture compare 1 event
		if (LL_TIM_IsActiveFlag_CC1(timHandle) && LL_TIM_IsEnabledIT_CC1(timHandle)) {
			LL_TIM_ClearFlag_CC1(timHandle);
			if ((timHandle->CCMR1 & TIM_CCMR1_CC1S) != 0x00U) {
				CallInputCaptureEvent(1);
			} else {
				CallOutputCompareEvent(1);
			}
		}


		// Capture compare 2 event
		if (LL_TIM_IsActiveFlag_CC2(timHandle) && LL_TIM_IsEnabledIT_CC2(timHandle)) {
			LL_TIM_ClearFlag_CC2(timHandle);
			if ((timHandle->CCMR1 & TIM_CCMR1_CC2S) != 0x00U) {
				CallInputCaptureEvent(2);
			} else {
				CallOutputCompareEvent(2);
			}
		}


		// Capture compare 3 event
		if (LL_TIM_IsActiveFlag_CC3(timHandle) && LL_TIM_IsEnabledIT_CC3(timHandle)) {
			LL_TIM_ClearFlag_CC3(timHandle);
			if ((timHandle->CCMR2 & TIM_CCMR2_CC3S) != 0x00U) {
				CallInputCaptureEvent(3);
			} else {
				CallOutputCompareEvent(3);
			}
		}


		// Capture compare 4 event
		if (LL_TIM_IsActiveFlag_CC4(timHandle) && LL_TIM_IsEnabledIT_CC4(timHandle)) {
			LL_TIM_ClearFlag_CC4(timHandle);
			if ((timHandle->CCMR2 & TIM_CCMR2_CC4S) != 0x00U) {
				CallInputCaptureEvent(4);
			} else {
				CallOutputCompareEvent(4);
			}
		}


		// TIM Update event
		if (LL_TIM_IsActiveFlag_UPDATE(timHandle) && LL_TIM_IsEnabledIT_UPDATE(timHandle)) {
			LL_TIM_ClearFlag_UPDATE(timHandle);
			CallUpdateEvent();
		}


		// TIM Break input event
		if (LL_TIM_IsActiveFlag_BRK(timHandle) && LL_TIM_IsEnabledIT_BRK(timHandle)) {
			LL_TIM_ClearFlag_BRK(timHandle);
			CallBreakInputEvent();
		}


		// TIM Trigger detection event
		if (LL_TIM_IsActiveFlag_TRIG(timHandle) && LL_TIM_IsEnabledIT_TRIG(timHandle)) {
			LL_TIM_ClearFlag_TRIG(timHandle);
			CallTriggerDetectionEvent();
		}


		// TIM commutation event
		if (LL_TIM_IsActiveFlag_COM(timHandle) && LL_TIM_IsEnabledIT_COM(timHandle)) {
			LL_TIM_ClearFlag_COM(timHandle);
			CallCommutationEvent();
		}
	}


public:
	virtual inline void EnableCounter(bool enableTimerCounter) override {
		if(enableTimerCounter) {
			LL_TIM_EnableCounter(timHandle);
		} else {
			LL_TIM_DisableCounter(timHandle);
		}
	}


	virtual inline void SetChannelsState(const std::initializer_list<EnableChannelParameters>& list) override {
		uint32 enableChannels = 0;
		uint32 disableChannels = 0;

		for(auto &channel : list) {
			switch (channel.channelSelect) {
				case ChannelEnableSelect::EnablePositive:
					enableChannels |= channel.channel.Get<1>();
				break;

				case ChannelEnableSelect::EnableNegative:
					enableChannels |= channel.channel.Get<2>();
				break;

				case ChannelEnableSelect::EnableAll:
					enableChannels |= channel.channel.Get<1>();
					enableChannels |= channel.channel.Get<2>();
				break;

				case ChannelEnableSelect::DisablePositive:
					disableChannels |= channel.channel.Get<1>();
				break;

				case ChannelEnableSelect::DisableNegative:
					disableChannels |= channel.channel.Get<2>();
				break;

				case ChannelEnableSelect::DisableAll:
					disableChannels |= channel.channel.Get<1>();
					disableChannels |= channel.channel.Get<2>();
				break;
			}
		}


		if(enableChannels != 0) {
			LL_TIM_CC_EnableChannel(timHandle, enableChannels);
		}

		if(disableChannels != 0) {
			LL_TIM_CC_DisableChannel(timHandle, disableChannels);
		}
	}


	virtual inline void SetDivision(ClockDivisionOption division) override {
		if (IS_TIM_CLOCK_DIVISION_INSTANCE(timHandle)) {
			LL_TIM_SetClockDivision(timHandle, division.Get());
			parameters.division = division;
		}
	}


	virtual inline void SetPrescaler(uint32 prescaler) override {
		SystemAssert(prescaler <= 0xFFFF);
		LL_TIM_SetPrescaler(timHandle, prescaler);
		parameters.prescaler = static_cast<uint16>(prescaler);
	}


	virtual inline void SetPeriod(uint32 period) override {
		LL_TIM_SetAutoReload(timHandle, period);
		parameters.period = period;
	}


	virtual inline void SetCompare(ChannelOption channel, uint32 compare) override {
		if(channel == Channel::C1) {
			LL_TIM_OC_SetCompareCH1(timHandle, compare);
		} else if(channel == Channel::C2) {
			LL_TIM_OC_SetCompareCH2(timHandle, compare);
		} else if(channel == Channel::C3) {
			LL_TIM_OC_SetCompareCH3(timHandle, compare);
		} else if(channel == Channel::C4) {
			LL_TIM_OC_SetCompareCH4(timHandle, compare);
		}
		outputCompareParameters[GetChannelIndex(channel)].compare = compare;
	}


	virtual inline void SetCompareMode(ChannelOption channel, OutputCompareOption mode) override {
		LL_TIM_OC_SetMode(timHandle, channel.Get<1>(), mode.Get());
	}


	virtual inline uint32 GetCapture(ChannelOption channel) override {
		if(channel == Channel::C1) {
			return LL_TIM_IC_GetCaptureCH1(timHandle);
		} else if(channel == Channel::C2) {
			return LL_TIM_IC_GetCaptureCH2(timHandle);
		} else if(channel == Channel::C3) {
			return LL_TIM_IC_GetCaptureCH3(timHandle);
		} else if(channel == Channel::C4) {
			return LL_TIM_IC_GetCaptureCH4(timHandle);
		}
		SystemAbort();
		return 0;
	}


	virtual inline void GenerateUpdateEvent() override {
		LL_TIM_GenerateEvent_UPDATE(timHandle);
	}


	virtual uint16 GetClockDivision() override {
		if(parameters.division == ClockDivision::D1) {
			return 1;
		} else if(parameters.division == ClockDivision::D2) {
			return 2;
		} else if(parameters.division == ClockDivision::D4) {
			return 4;
		}

		return 1;
	}


	virtual uint8 GetChannelIndex(ChannelOption channel) override {
		if(channel == Channel::C1) {
			return 0;
		}
		if(channel == Channel::C2) {
			return 1;
		}
		if(channel == Channel::C3) {
			return 2;
		}
		if(channel == Channel::C4) {
			return 3;
		}

		SystemAbort();
		return 0;
	}


protected:
	virtual ResultStatus Initialization() override {
		SystemAssert(parameters.prescaler <= 0xFFFF);
		SystemAssert(parameters.repetitionCounter <= 0xFF);

		auto status = BeforeInitialization();
		if(status != ResultStatus::ok) {
			return status;
		}


		LL_TIM_InitTypeDef init = {
			.Prescaler = static_cast<uint16>(parameters.prescaler),
			.CounterMode = parameters.mode.Get(),
			.Autoreload = parameters.period,
			.ClockDivision = parameters.division.Get(),
			.RepetitionCounter = static_cast<uint8>(parameters.repetitionCounter)
		};

		if(LL_TIM_Init(timHandle, &init) != ErrorStatus::SUCCESS) {
			return ResultStatus::error;
		}

		// ARR preload is off: a SetPeriod() write takes effect at once, not at the next update event.
		LL_TIM_DisableARRPreload(timHandle);

		return AfterInitialization();
	}


	virtual ResultStatus OutputCompareInitialization(const std::initializer_list<OutputCompareParameters>& list) override {
		for(auto &channel : list) {
			LL_TIM_OC_EnablePreload(timHandle, channel.channel.Get<1>()); // TODO: only 1 ??

			LL_TIM_OC_InitTypeDef init = {
				.OCMode = channel.mode.Get(),

				.OCState = LL_TIM_OCSTATE_DISABLE,
				.OCNState = LL_TIM_OCSTATE_DISABLE,

				.CompareValue = channel.compare,

				.OCPolarity = LL_TIM_OCPOLARITY_HIGH,
				.OCNPolarity = LL_TIM_OCPOLARITY_HIGH,

				.OCIdleState = LL_TIM_OCIDLESTATE_LOW,
				.OCNIdleState = LL_TIM_OCIDLESTATE_LOW
			};
			LL_TIM_OC_DisableFast(timHandle, channel.channel.Get<1>()); // TODO: channel 2 ??

			if(LL_TIM_OC_Init(timHandle, channel.channel.Get<1>(), &init) != ErrorStatus::SUCCESS) {
				return ResultStatus::error;
			}
		}

		// TODO: Add Trigger to settings
		LL_TIM_SetTriggerOutput(timHandle, LL_TIM_TRGO_RESET);

		// TODO: Add Master Slave Mode to settings
		LL_TIM_DisableMasterSlaveMode(timHandle);

		// TIM8 is absent on F401 and F411, where TIM1 is the only advanced timer. Compile the TIM8 compare only where it exists.
			#if defined(TIM8)
			if(timHandle == TIM1 || timHandle == TIM8) {
			#else
			if(timHandle == TIM1) {
			#endif
			LL_TIM_EnableAllOutputs(timHandle);
		}


		return ResultStatus::ok;
	}


	// The select, prescaler, filter and polarity of each channel are written to CCMR1 or CCMR2 and to CCER by the LL functions.
	// The remapping is written to OR. A remapping of zero writes nothing, so the default option leaves OR as it is.
	virtual ResultStatus InputCaptureInitialization(const std::initializer_list<InputCaptureParameters>& list) override {
		for(const InputCaptureParameters &channel : list) {
			LL_TIM_IC_SetActiveInput(timHandle, channel.channel.Get<1>(), CastInputSelection(channel));
			LL_TIM_IC_SetPrescaler(timHandle, channel.channel.Get<1>(), channel.prescaler.Get());
			LL_TIM_IC_SetFilter(timHandle, channel.channel.Get<1>(), channel.filter.Get());
			LL_TIM_IC_SetPolarity(timHandle, channel.channel.Get<1>(), CastInputPolarity(channel));
			if (channel.remapping.Get() != 0) {
				LL_TIM_SetRemap(timHandle, channel.remapping.Get());
			}
		}

		return ResultStatus::ok;
	}


	virtual ResultStatus BreakAndDeadTimeInitialization(const std::initializer_list<BreakAndDeadTimeParameters>&) override {
		return ResultStatus::notSupported;
	}


	virtual ResultStatus SetInterrupt(InterruptOption interrupt, bool enable) {
		if(interrupt == Interrupt::CaptureCompare1) {
			if (enable) {
				LL_TIM_EnableIT_CC1(timHandle);
			} else {
				LL_TIM_DisableIT_CC1(timHandle);
			}
		} else if(interrupt == Interrupt::CaptureCompare2) {
			if (enable) {
				LL_TIM_EnableIT_CC2(timHandle);
			} else {
				LL_TIM_DisableIT_CC2(timHandle);
			}
		} else if(interrupt == Interrupt::CaptureCompare3) {
			if (enable) {
				LL_TIM_EnableIT_CC3(timHandle);
			} else {
				LL_TIM_DisableIT_CC3(timHandle);
			}
		} else if(interrupt == Interrupt::CaptureCompare4) {
			if (enable) {
				LL_TIM_EnableIT_CC4(timHandle);
			} else {
				LL_TIM_DisableIT_CC4(timHandle);
			}
		} else if(interrupt == Interrupt::Update) {
			if (enable) {
				LL_TIM_EnableIT_UPDATE(timHandle);
			} else {
				LL_TIM_DisableIT_UPDATE(timHandle);
			}
		} else if(interrupt == Interrupt::Break) {
			if (enable) {
				LL_TIM_EnableIT_BRK(timHandle);
			} else {
				LL_TIM_DisableIT_BRK(timHandle);
			}
		} else if(interrupt == Interrupt::Trigger) {
			if (enable) {
				LL_TIM_EnableIT_TRIG(timHandle);
			} else {
				LL_TIM_DisableIT_TRIG(timHandle);
			}
		} else if(interrupt == Interrupt::Commutation) {
			if (enable) {
				LL_TIM_EnableIT_COM(timHandle);
			} else {
				LL_TIM_DisableIT_COM(timHandle);
			}
		} else {
			// A value outside the Interrupt table is a caller error. Report it instead of returning ok with nothing enabled.
			return ResultStatus::notSupported;
		}
		return ResultStatus::ok;
	}


private:
	constexpr uint32 CastInputSelection(const InputCaptureParameters& channel) const {
		switch (channel.selection) {
			case InputSelection::Direct:   return LL_TIM_ACTIVEINPUT_DIRECTTI;
			case InputSelection::Indirect: return LL_TIM_ACTIVEINPUT_INDIRECTTI;
			case InputSelection::TRC:      return LL_TIM_ACTIVEINPUT_TRC;
		}
		SystemAbort();
		return 0;
	}


	constexpr uint32 CastInputPolarity(const InputCaptureParameters& channel) const {
		switch (channel.polarity) {
			case InputPolarity::Rising:        return LL_TIM_IC_POLARITY_RISING;
			case InputPolarity::Falling:       return LL_TIM_IC_POLARITY_FALLING;
			case InputPolarity::RisingFalling: return LL_TIM_IC_POLARITY_BOTHEDGE;
		}
		SystemAbort();
		return 0;
	}
};















