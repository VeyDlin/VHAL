#pragma once
#include "../../TIMAdapter.h"




using ATIM = class TIMAdapterF4;


class TIMAdapterF4: public TIMAdapter {
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
		static inline constexpr OutputCompareOption Forzen         { LL_TIM_OCMODE_FROZEN };
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
		if (LL_TIM_IsActiveFlag_CC1(timHandle) && LL_TIM_IsEnabledIT_CC1(timHandle)) {
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
			if ((timHandle->CCMR1 & TIM_CCMR2_CC3S) != 0x00U) {
				CallInputCaptureEvent(3);
			} else {
				CallOutputCompareEvent(3);
			}
		}


		// Capture compare 4 event
		if (LL_TIM_IsActiveFlag_CC3(timHandle) && LL_TIM_IsEnabledIT_CC3(timHandle)) {
			LL_TIM_ClearFlag_CC4(timHandle);
			if ((timHandle->CCMR1 & TIM_CCMR2_CC4S) != 0x00U) {
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
			auto CR1_REG = LL_TIM_ReadReg(timHandle, CR1);
			MODIFY_REG(CR1_REG, TIM_CR1_CKD, division.Get());
			parameters.division = division;
		}
	}





	virtual inline void SetPrescaler(uint32 prescaler) override {
		SystemAssert(parameters.prescaler <= 0xFFFF);
		LL_TIM_SetPrescaler(timHandle, prescaler);
		parameters.prescaler = prescaler;
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
	virtual Status::statusType Initialization() override {
		SystemAssert(parameters.prescaler <= 0xFFFF);
		SystemAssert(parameters.repetitionCounter <= 0xFF);

		auto status = BeforeInitialization();
		if(status != Status::ok) {
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
			return Status::error;
		}

		LL_TIM_DisableARRPreload(timHandle);

		return AfterInitialization();
	}





	virtual Status::statusType OutputCompareInitialization(const std::initializer_list<OutputCompareParameters>& list) override {
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
				return Status::error;
			}
		}

		// TODO: Add Trigger to settings
		LL_TIM_SetTriggerOutput(timHandle, LL_TIM_TRGO_RESET);

		// TODO: Add Master Slave Mode to settings
		LL_TIM_DisableMasterSlaveMode(timHandle);

		if(timHandle == TIM1 || timHandle == TIM8) {
			LL_TIM_EnableAllOutputs(timHandle);
		}


		return Status::ok;
	}





	virtual Status::statusType InputCaptureInitialization(const std::initializer_list<InputCaptureParameters>& list) override {
		return Status::notSupported;
	}





	virtual Status::statusType BreakAndDeadTimeInitialization(const std::initializer_list<BreakAndDeadTimeParameters>& list) override {
		return Status::notSupported;
	}





	virtual Status::statusType SetInterrupt(InterruptOption interrupt, bool enable) {
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
		}
		return Status::ok;
	}
};















