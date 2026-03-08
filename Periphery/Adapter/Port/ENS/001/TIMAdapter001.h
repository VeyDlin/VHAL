#pragma once
#include <Adapter/TIMAdapter.h>


using ATIM = class TIMAdapter001;

class TIMAdapter001 : public TIMAdapter<CMSDK_TIMER_TypeDef> {
public:
	struct ClockDivision {
		static inline constexpr ClockDivisionOption D1 { 0 };
	};

	struct Counter {
		static inline constexpr CounterOption Down { 0 };
	};

	struct OutputCompare {
	};

	struct Channel {
	};

	struct Interrupt {
		static inline constexpr InterruptOption Update { 1 };
	};

	struct OutputTrigger {
	};



public:
	TIMAdapter001() { }
	TIMAdapter001(CMSDK_TIMER_TypeDef *timer, uint32 busClockHz, Bitness bit = Bitness::B32): TIMAdapter(timer, busClockHz, bit) { }



	virtual inline void IrqHandler() override {
		if (timHandle->INTSTATUS & 0x01) {
			timHandle->INTCLEAR = 0x01;
			CallUpdateEvent();
		}
	}



public:
	virtual inline void EnableCounter(bool enableTimerCounter) override {
		if (enableTimerCounter) {
			timHandle->CTRL |= (1 << 0); // EN
		} else {
			timHandle->CTRL &= ~(1 << 0);
		}
	}



	virtual inline void SetChannelsState(const std::initializer_list<EnableChannelParameters>& list) override {
		// No channels available on ENS001 simple timer
	}



	virtual inline void SetDivision(ClockDivisionOption division) override {
		// No clock division on ENS001 simple timer
		parameters.division = division;
	}



	virtual inline void SetPrescaler(uint32 prescaler) override {
		// ENS001 simple timer has no prescaler register
		// Non-zero prescaler values are not supported
		parameters.prescaler = 0;
	}



	virtual inline void SetPeriod(uint32 period) override {
		timHandle->RELOAD = period;
		parameters.period = period;
	}



	virtual inline void SetCompare(ChannelOption channel, uint32 compare) override {
		// No output compare on ENS001 simple timer
	}



	virtual inline void SetCompareMode(ChannelOption channel, OutputCompareOption mode) override {
		// No output compare on ENS001 simple timer
	}



	virtual inline uint32 GetCapture(ChannelOption channel) override {
		// No input capture on ENS001 simple timer
		return 0;
	}



	virtual inline void GenerateUpdateEvent() override {
		// Manually reload: set VALUE = RELOAD
		timHandle->VALUE = timHandle->RELOAD;
	}



	virtual uint16 GetClockDivision() override {
		return 1;
	}



	virtual uint8 GetChannelIndex(ChannelOption channel) override {
		return 0;
	}



private:
	inline void OnEnableClock() {
#ifdef CMSDK_TIMER0
		if (timHandle == CMSDK_TIMER0) {
			CMSDK_SYSCON->APB_CLKEN |= (1 << 8);
			return;
		}
#endif
#ifdef CMSDK_TIMER1
		if (timHandle == CMSDK_TIMER1) {
			CMSDK_SYSCON->APB_CLKEN |= (1 << 9);
			return;
		}
#endif
	}


protected:
	virtual ResultStatus Initialization() override {
		OnEnableClock();

		auto status = BeforeInitialization();
		if (status != ResultStatus::ok) {
			return status;
		}

		// Disable timer before configuration
		timHandle->CTRL = 0;

		// Set reload value
		timHandle->RELOAD = parameters.period;

		// Load the initial value
		timHandle->VALUE = parameters.period;

		// Clear any pending interrupt
		timHandle->INTCLEAR = 0x01;

		return AfterInitialization();
	}



	virtual ResultStatus OutputCompareInitialization(const std::initializer_list<OutputCompareParameters>& list) override {
		return ResultStatus::notSupported;
	}



	virtual ResultStatus InputCaptureInitialization(const std::initializer_list<InputCaptureParameters>& list) override {
		return ResultStatus::notSupported;
	}



	virtual ResultStatus BreakAndDeadTimeInitialization(const std::initializer_list<BreakAndDeadTimeParameters>& list) override {
		return ResultStatus::notSupported;
	}



	virtual ResultStatus SetInterrupt(InterruptOption interrupt, bool enable) override {
		if (interrupt == Interrupt::Update) {
			if (enable) {
				timHandle->CTRL |= (1 << 3); // IRQEN
			} else {
				timHandle->CTRL &= ~(1 << 3);
			}
			return ResultStatus::ok;
		}
		return ResultStatus::notSupported;
	}

};
