#pragma once
#include "../../TIMAdapter.h"




using ATIM = class TIMAdapterG0;


class TIMAdapterG0: public TIMAdapter {
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
		static inline constexpr OutputCompareOption Active         { LL_TIM_OCMODE_ACTIVE };
		static inline constexpr OutputCompareOption Inactive       { LL_TIM_OCMODE_INACTIVE };
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

	struct OutputTrigger {
		static inline constexpr OutputTriggerOption Reset           { LL_TIM_TRGO_RESET };
		static inline constexpr OutputTriggerOption Enable          { LL_TIM_TRGO_ENABLE };
		static inline constexpr OutputTriggerOption Update          { LL_TIM_TRGO_UPDATE };
		static inline constexpr OutputTriggerOption CaptureCompare1 { LL_TIM_TRGO_CC1IF };
		static inline constexpr OutputTriggerOption OC1REF          { LL_TIM_TRGO_OC1REF };
		static inline constexpr OutputTriggerOption OC2REF          { LL_TIM_TRGO_OC2REF };
		static inline constexpr OutputTriggerOption OC3REF          { LL_TIM_TRGO_OC3REF };
		static inline constexpr OutputTriggerOption OC4REF          { LL_TIM_TRGO_OC4REF };
	};

	struct OutputTrigger2 {
		static inline constexpr OutputTriggerOption2 Reset             { LL_TIM_TRGO2_RESET };
		static inline constexpr OutputTriggerOption2 Enable            { LL_TIM_TRGO2_ENABLE };
		static inline constexpr OutputTriggerOption2 Update            { LL_TIM_TRGO2_UPDATE };
		static inline constexpr OutputTriggerOption2 CaptureCompare1   { LL_TIM_TRGO2_CC1F };
		static inline constexpr OutputTriggerOption2 OC1REF            { LL_TIM_TRGO2_OC1 };
		static inline constexpr OutputTriggerOption2 OC2REF            { LL_TIM_TRGO2_OC2 };
		static inline constexpr OutputTriggerOption2 OC3REF            { LL_TIM_TRGO2_OC3 };
		static inline constexpr OutputTriggerOption2 OC4REF            { LL_TIM_TRGO2_OC4 };
		static inline constexpr OutputTriggerOption2 OC5REF            { LL_TIM_TRGO2_OC5 };
		static inline constexpr OutputTriggerOption2 OC6REF            { LL_TIM_TRGO2_OC6 };
		static inline constexpr OutputTriggerOption2 RisingFalling4    { LL_TIM_TRGO2_OC4_RISINGFALLING };
		static inline constexpr OutputTriggerOption2 RisingFalling6    { LL_TIM_TRGO2_OC6_RISINGFALLING };
		static inline constexpr OutputTriggerOption2 Rising4Rising6    { LL_TIM_TRGO2_OC4_RISING_OC6_RISING };
		static inline constexpr OutputTriggerOption2 Rising4Falling6   { LL_TIM_TRGO2_OC4_RISING_OC6_FALLING };
		static inline constexpr OutputTriggerOption2 Rising5Rising6    { LL_TIM_TRGO2_OC5_RISING_OC6_RISING };
		static inline constexpr OutputTriggerOption2 Rising5Falling6   { LL_TIM_TRGO2_OC5_RISING_OC6_FALLING };
	};





public:
	TIMAdapterG0() { }
	TIMAdapterG0(TIM_TypeDef *timer, uint32 busClockHz, Bitness bit = Bitness::B16): TIMAdapter(timer, busClockHz, bit) { }



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


		// TIM Break 2 input event
		if (LL_TIM_IsActiveFlag_BRK2(timHandle) && LL_TIM_IsEnabledIT_BRK(timHandle)) {
			LL_TIM_ClearFlag_BRK2(timHandle);
			CallBreak2InputEvent();
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
					enableChannels |= channel.channel->GetCode(1);
				break;

				case ChannelEnableSelect::EnableNegative:
					enableChannels |= channel.channel->GetCode(2);
				break;

				case ChannelEnableSelect::EnableAll:
					enableChannels |= channel.channel->GetCode(1);
					enableChannels |= channel.channel->GetCode(2);
				break;

				case ChannelEnableSelect::DisablePositive:
					disableChannels |= channel.channel->GetCode(1);
				break;

				case ChannelEnableSelect::DisableNegative:
					disableChannels |= channel.channel->GetCode(2);
				break;

				case ChannelEnableSelect::DisableAll:
					disableChannels |= channel.channel->GetCode(1);
					disableChannels |= channel.channel->GetCode(2);
				break;
			}
		}


		if(enableChannels != 0) {
			// TODO:: alwis CC?
			LL_TIM_CC_EnableChannel(timHandle, enableChannels);
		}

		if(disableChannels != 0) {
			// TODO:: alwis CC?
			LL_TIM_CC_DisableChannel(timHandle, disableChannels);
		}
	}





	virtual inline void SetDivision(const ClockDivisionMode *division) override {
		if (IS_TIM_CLOCK_DIVISION_INSTANCE(timHandle)) {
			auto CR1_REG = LL_TIM_ReadReg(timHandle, CR1);
			MODIFY_REG(CR1_REG, TIM_CR1_CKD, division->GetCode());
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





	virtual inline void SetCompare(const ChannelMode *channel, uint32 compare) override {
		if(channel == Channel::C1()) {
			LL_TIM_OC_SetCompareCH1(timHandle, compare);
		} else if(channel == Channel::C2()) {
			LL_TIM_OC_SetCompareCH2(timHandle, compare);
		} else if(channel == Channel::C3()) {
			LL_TIM_OC_SetCompareCH3(timHandle, compare);
		} else if(channel == Channel::C4()) {
			LL_TIM_OC_SetCompareCH4(timHandle, compare);
		}
	}





	virtual inline void GenerateUpdateEvent() override {
		LL_TIM_GenerateEvent_UPDATE(timHandle);
	}





	virtual uint16 GetClockDivision() override {
		if(parameters.division == ClockDivision::D1()) {
			return 1;
		} else if(parameters.division == ClockDivision::D2()) {
			return 2;
		} else if(parameters.division == ClockDivision::D4()) {
			return 4;
		}

		return 1;
	}





	virtual uint8 GetChannelIndex(const ChannelMode *channel) override {
		if(channel == Channel::C1()) {
			return 0;
		}
		if(channel == Channel::C2()) {
			return 1;
		}
		if(channel == Channel::C3()) {
			return 2;
		}
		if(channel == Channel::C4()) {
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
			.CounterMode = parameters.mode->GetCode(),
			.Autoreload = parameters.period,
			.ClockDivision = parameters.division->GetCode(),
			.RepetitionCounter = static_cast<uint8>(parameters.repetitionCounter)
		};

		if(LL_TIM_Init(timHandle, &init) != ErrorStatus::SUCCESS) {
			return Status::error;
		}

		// TODO: ??
		LL_TIM_DisableARRPreload(timHandle);

		return AfterInitialization();
	}





	virtual Status::statusType OutputCompareInitialization(const std::initializer_list<OutputCompareParameters>& list) override {
		for(auto &channel : list) {
			LL_TIM_OC_EnablePreload(timHandle, channel.channel->GetCode(1)); // TODO: only 1 ??

			LL_TIM_OC_InitTypeDef init = {
				.OCMode = channel.mode->GetCode(),

				.OCState = CastState(channel.positive.state),
				.OCNState = CastState(channel.negative.state),

				.CompareValue = channel.compare,

				.OCPolarity = CastPolarity(channel.positive.polarity),
				.OCNPolarity = CastPolarity(channel.negative.polarity),

				.OCIdleState = CastIdleState(channel.positive.idleState),
				.OCNIdleState = CastIdleState(channel.negative.idleState)
			};
			LL_TIM_OC_DisableFast(timHandle, channel.channel->GetCode(2)); // TODO: channel 2 ??

			if(LL_TIM_OC_Init(timHandle, channel.channel->GetCode(1), &init) != ErrorStatus::SUCCESS) {
				return Status::error;
			}
		}

		// TODO: ClockSource?
		//LL_TIM_SetClockSource(timHandle, LL_TIM_CLOCKSOURCE_INTERNAL);

		// TODO: Add Trigger to settings
		LL_TIM_SetTriggerOutput(timHandle, LL_TIM_TRGO_RESET);

		if(IS_TIM_TRGO2_INSTANCE(timHandle)) {
			// TODO: Add Trigger 2 to settings
			LL_TIM_SetTriggerOutput2(timHandle, LL_TIM_TRGO2_RESET);
		}

		// TODO: Add Master Slave Mode to settings
		LL_TIM_DisableMasterSlaveMode(timHandle);

		if(timHandle == TIM1) {
			LL_TIM_EnableAllOutputs(timHandle);
		}
		
		return Status::ok;
	}





	virtual Status::statusType BreakAndDeadTimeInitialization(const std::initializer_list<BreakAndDeadTimeParameters>& list) override {
		return Status::notSupported;
	}





	virtual Status::statusType SetInterrupt(const InterruptType *interrupt, bool enable) {
		if(interrupt == Interrupt::CaptureCompare1()) {
			if (enable) {
				LL_TIM_EnableIT_CC1(timHandle);
			} else {
				LL_TIM_DisableIT_CC1(timHandle);
			}
		} else if(interrupt == Interrupt::CaptureCompare2()) {
			if (enable) {
				LL_TIM_EnableIT_CC2(timHandle);
			} else {
				LL_TIM_DisableIT_CC2(timHandle);
			}
		} else if(interrupt == Interrupt::CaptureCompare3()) {
			if (enable) {
				LL_TIM_EnableIT_CC3(timHandle);
			} else {
				LL_TIM_DisableIT_CC3(timHandle);
			}
		} else if(interrupt == Interrupt::CaptureCompare4()) {
			if (enable) {
				LL_TIM_EnableIT_CC4(timHandle);
			} else {
				LL_TIM_DisableIT_CC4(timHandle);
			}
		} else if(interrupt == Interrupt::Update()) {
			if (enable) {
				LL_TIM_EnableIT_UPDATE(timHandle);
			} else {
				LL_TIM_DisableIT_UPDATE(timHandle);
			}
		} else if(interrupt == Interrupt::Break()) {
			if (enable) {
				LL_TIM_EnableIT_BRK(timHandle);
			} else {
				LL_TIM_DisableIT_BRK(timHandle);
			}
		} else if(interrupt == Interrupt::Trigger()) {
			if (enable) {
				LL_TIM_EnableIT_TRIG(timHandle);
			} else {
				LL_TIM_DisableIT_TRIG(timHandle);
			}
		} else if(interrupt == Interrupt::Commutation()) {
			if (enable) {
				LL_TIM_EnableIT_COM(timHandle);
			} else {
				LL_TIM_DisableIT_COM(timHandle);
			}
		}
		return Status::ok;
	}



private:
	constexpr uint32 CastPolarity(Polarity polarity) const {
		switch (polarity) {
			case Polarity::High: return LL_TIM_OCPOLARITY_HIGH;
			case Polarity::Low: return LL_TIM_OCPOLARITY_LOW;
		}
		SystemAbort();
		return 0;
	}



	constexpr uint32 CastState(State state) const {
		switch (state) {
			case State::Enable: return LL_TIM_OCSTATE_ENABLE;
			case State::Disable: return LL_TIM_OCSTATE_DISABLE;
		}
		SystemAbort();
		return 0;
	}



	constexpr uint32 CastIdleState(IdleState idleState) const {
		switch (idleState) {
			case IdleState::High: return LL_TIM_OCIDLESTATE_HIGH;
			case IdleState::Low: return LL_TIM_OCIDLESTATE_LOW;
		}
		SystemAbort();
		return 0;
	}
};















