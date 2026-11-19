#pragma once
#include <Adapter/COMPAdapter.h>


// COMP (comparator) adapter for STM32G0 (RM0444, COMP chapter). It covers COMP1 and COMP2 on every G0 chip and COMP3
// where the chip defines it (STM32G0B1xx, STM32G0C1xx). STM32G071xx has no COMP3.
//
// The BSP must do the following before the first Initialization():
//   - Enable the SYSCFG bus clock (RCC APBENR2.SYSCFGEN, LL_APB2_GRP1_PERIPH_SYSCFG). The comparator registers sit in the
//     SYSCFG block, and HAL_COMP_Init() enables this clock for the same reason. While it is off, Initialization() returns notReady.
//   - Provide no separate comparator clock source. The comparators run from the bus clock, so there is no prescaler to set.
//   - Enable the ADC1_COMP_IRQn vector (vector 12, shared with ADC1) and forward ADC1_COMP_IRQHandler to IrqHandler() of every
//     comparator adapter. IrqHandler() serves only the EXTI line of its own comparator (17, 18 or 20) and ignores the ADC flags,
//     so one shared vector is safe. The adapter never touches NVIC.
//
// Window mode is not exposed. COMP12_COMMON and COMP23_COMMON hold the WINMODE and WINOUT bits shared by a pair of
// comparators. The adapter never writes them, so they keep the reset value (window mode off). The BSP must not enable window
// mode on a comparator this adapter drives.


using ACOMP = class COMPAdapterG0;


class COMPAdapterG0: public COMPAdapter<COMP_TypeDef> {
public:
	struct InputPlus {
		static inline constexpr InputPlusOption IO1 { LL_COMP_INPUT_PLUS_IO1 };
		static inline constexpr InputPlusOption IO2 { LL_COMP_INPUT_PLUS_IO2 };
		static inline constexpr InputPlusOption IO3 { LL_COMP_INPUT_PLUS_IO3 };
	};


	struct InputMinus {
		static inline constexpr InputMinusOption VRefInt1_4   { LL_COMP_INPUT_MINUS_1_4VREFINT };
		static inline constexpr InputMinusOption VRefInt1_2   { LL_COMP_INPUT_MINUS_1_2VREFINT };
		static inline constexpr InputMinusOption VRefInt3_4   { LL_COMP_INPUT_MINUS_3_4VREFINT };
		static inline constexpr InputMinusOption VRefInt      { LL_COMP_INPUT_MINUS_VREFINT };
		static inline constexpr InputMinusOption DAC1Channel1 { LL_COMP_INPUT_MINUS_DAC1_CH1 };
		static inline constexpr InputMinusOption DAC1Channel2 { LL_COMP_INPUT_MINUS_DAC1_CH2 };
		static inline constexpr InputMinusOption IO1          { LL_COMP_INPUT_MINUS_IO1 };
		static inline constexpr InputMinusOption IO2          { LL_COMP_INPUT_MINUS_IO2 };
		static inline constexpr InputMinusOption IO3          { LL_COMP_INPUT_MINUS_IO3 };
	};


	struct InputHysteresis {
		static inline constexpr InputHysteresisOption None   { LL_COMP_HYSTERESIS_NONE };
		static inline constexpr InputHysteresisOption Low    { LL_COMP_HYSTERESIS_LOW };
		static inline constexpr InputHysteresisOption Medium { LL_COMP_HYSTERESIS_MEDIUM };
		static inline constexpr InputHysteresisOption High   { LL_COMP_HYSTERESIS_HIGH };
	};


	// The G0 blanking sources are common to all comparator instances, so there is one list for COMP1, COMP2 and COMP3.
	struct OutputBlankingSource {
		static inline constexpr OutputBlankingSourceOption None                  { LL_COMP_BLANKINGSRC_NONE };
		static inline constexpr OutputBlankingSourceOption Timer1OutputCompare4  { LL_COMP_BLANKINGSRC_TIM1_OC4 };
		static inline constexpr OutputBlankingSourceOption Timer1OutputCompare5  { LL_COMP_BLANKINGSRC_TIM1_OC5 };
		static inline constexpr OutputBlankingSourceOption Timer2OutputCompare3  { LL_COMP_BLANKINGSRC_TIM2_OC3 };
		static inline constexpr OutputBlankingSourceOption Timer3OutputCompare3  { LL_COMP_BLANKINGSRC_TIM3_OC3 };
		static inline constexpr OutputBlankingSourceOption Timer15OutputCompare2 { LL_COMP_BLANKINGSRC_TIM15_OC2 };
	};


	COMPAdapterG0() = default;
	COMPAdapterG0(COMP_TypeDef *comp):COMPAdapter(comp) { }


	virtual inline void IrqHandler() override {
		uint32 line = GetExtiLine(compHandle);

		// Event-only lines still set their pending flags, because G0 sets them whenever the edge is enabled in RTSR/FTSR,
		// regardless of IMR. The shared ADC1_COMP vector would then report an event as an interrupt, so the handler
		// returns while the interrupt is disabled for this line and leaves the flags to the event path.
		if (LL_EXTI_IsEnabledIT_0_31(line) == 0) {
			return;
		}

		// G0 keeps rising and falling pending flags apart. The shared ADC1_COMP vector calls every comparator adapter,
		// so only the own EXTI line counts here.
		bool active = LL_EXTI_IsActiveRisingFlag_0_31(line) != 0 || LL_EXTI_IsActiveFallingFlag_0_31(line) != 0;
		if (!active) {
			return;
		}

		LL_EXTI_ClearRisingFlag_0_31(line);
		LL_EXTI_ClearFallingFlag_0_31(line);

		if (onInterrupt) {
			onInterrupt(GetOutputLevel());
		}
	}


	virtual bool GetOutputLevel() override {
		return LL_COMP_ReadOutputLevel(compHandle) == LL_COMP_OUTPUT_LEVEL_HIGH;
	}


	virtual ResultStatus Enable() override {
		// Without the SYSCFG clock the comparator register writes are dropped. Report it instead of returning ok.
		if (!LL_APB2_GRP1_IsEnabledClock(LL_APB2_GRP1_PERIPH_SYSCFG)) {
			return ResultStatus::notReady;
		}

		if (LL_COMP_IsEnabled(compHandle)) {
			return ResultStatus::ok;
		}

		LL_COMP_Enable(compHandle);

		// The VrefInt scaler needs longer to settle than the comparator startup, so the wait depends on the input.
		if (IsVRefIntInput(parameters.inputMinus)) {
			System::DelayUs(LL_COMP_DELAY_VOLTAGE_SCALER_STAB_US);
		} else {
			System::DelayUs(LL_COMP_DELAY_STARTUP_US);
		}

		return ResultStatus::ok;
	}


	virtual ResultStatus Disable() override {
		if (LL_COMP_IsEnabled(compHandle)) {
			LL_COMP_Disable(compHandle);
		}

		return ResultStatus::ok;
	}


	virtual ResultStatus SetInputPlus(InputPlusOption inputPlus) override {
		LL_COMP_SetInputPlus(compHandle, inputPlus.Get());
		parameters.inputPlus = inputPlus;
		return ResultStatus::ok;
	}


	virtual ResultStatus SetInputMinus(InputMinusOption inputMinus) override {
		// Read the previous input before the register write, to see whether the scaler is newly selected.
		bool wasVRefInt = IsVRefIntInput(parameters.inputMinus);

		LL_COMP_SetInputMinus(compHandle, inputMinus.Get());
		parameters.inputMinus = inputMinus;

		// A running comparator needs the scaler to settle when it is newly selected. A disabled comparator does not need
		// the wait here, because Enable() applies it when the comparator starts. Switching between two scaler inputs keeps
		// the scaler running, so no wait is needed then either.
		if (LL_COMP_IsEnabled(compHandle) && !wasVRefInt && IsVRefIntInput(inputMinus)) {
			System::DelayUs(LL_COMP_DELAY_VOLTAGE_SCALER_STAB_US);
		}

		return ResultStatus::ok;
	}


	virtual ResultStatus SetInputHysteresis(InputHysteresisOption inputHysteresis) override {
		LL_COMP_SetInputHysteresis(compHandle, inputHysteresis.Get());
		parameters.inputHysteresis = inputHysteresis;
		return ResultStatus::ok;
	}


	virtual ResultStatus SetOutputPolarity(OutputPolarity polarity) override {
		// Cast the new value, not the stored one: the stored value is updated after the register write.
		LL_COMP_SetOutputPolarity(compHandle, CastOutputPolarity(polarity));
		parameters.outputPolarity = polarity;
		return ResultStatus::ok;
	}


	virtual ResultStatus SetOutputBlankingSource(OutputBlankingSourceOption outputBlankingSource) override {
		LL_COMP_SetOutputBlankingSource(compHandle, outputBlankingSource.Get());
		parameters.outputBlankingSource = outputBlankingSource;
		return ResultStatus::ok;
	}


	virtual ResultStatus SetTriggerMode(TriggerMode triggerMode) override {
		uint32 extiLine = GetExtiLine(compHandle);
		const uint8 triggerFlags = static_cast<uint8>(triggerMode);

		// G0 puts COMP1, COMP2 and COMP3 on EXTI lines 0..31, so only the _0_31 registers are used.
		if ((triggerFlags & TriggerModeFlags::Rising) != 0) {
			LL_EXTI_EnableRisingTrig_0_31(extiLine);
		} else {
			LL_EXTI_DisableRisingTrig_0_31(extiLine);
		}

		if ((triggerFlags & TriggerModeFlags::Falling) != 0) {
			LL_EXTI_EnableFallingTrig_0_31(extiLine);
		} else {
			LL_EXTI_DisableFallingTrig_0_31(extiLine);
		}

		// Clear a flag left from before the reconfiguration, so it does not fire the new interrupt.
		LL_EXTI_ClearRisingFlag_0_31(extiLine);
		LL_EXTI_ClearFallingFlag_0_31(extiLine);

		if ((triggerFlags & TriggerModeFlags::Event) != 0) {
			LL_EXTI_EnableEvent_0_31(extiLine);
		} else {
			LL_EXTI_DisableEvent_0_31(extiLine);
		}

		if ((triggerFlags & TriggerModeFlags::Interrupt) != 0) {
			LL_EXTI_EnableIT_0_31(extiLine);
		} else {
			LL_EXTI_DisableIT_0_31(extiLine);
		}

		parameters.triggerMode = triggerMode;
		return ResultStatus::ok;
	}


protected:
	virtual ResultStatus Initialization() override {
		// Checked before BeforeInitialization(): the hook must not run while the registers are not clocked.
		if (!LL_APB2_GRP1_IsEnabledClock(LL_APB2_GRP1_PERIPH_SYSCFG)) {
			return ResultStatus::notReady;
		}

		ResultStatus status = BeforeInitialization();
		if (status != ResultStatus::ok) {
			return status;
		}

		// PowerMode is not part of the contract. HIGHSPEED is the reset value, so the adapter keeps the reset behaviour.
		LL_COMP_InitTypeDef init = {
			.PowerMode = LL_COMP_POWERMODE_HIGHSPEED,
			.InputPlus = parameters.inputPlus.Get(),
			.InputMinus = parameters.inputMinus.Get(),
			.InputHysteresis = parameters.inputHysteresis.Get(),
			.OutputPolarity = CastOutputPolarity(parameters.outputPolarity),
			.OutputBlankingSource = parameters.outputBlankingSource.Get()
		};

		SystemAssert(LL_COMP_Init(compHandle, &init) == ErrorStatus::SUCCESS);
		System::DelayUs(LL_COMP_DELAY_VOLTAGE_SCALER_STAB_US);

		SetTriggerMode(parameters.triggerMode);

		return AfterInitialization();
	}


private:
	// The four VrefInt fractions are the only minus inputs routed through the voltage scaler, so they share one settling time.
	static bool IsVRefIntInput(InputMinusOption inputMinus) {
		return inputMinus == InputMinus::VRefInt ||
			inputMinus == InputMinus::VRefInt1_2 ||
			inputMinus == InputMinus::VRefInt1_4 ||
			inputMinus == InputMinus::VRefInt3_4;
	}


	static uint32 CastOutputPolarity(OutputPolarity polarity) {
		switch (polarity) {
			case OutputPolarity::NotInverted: return LL_COMP_OUTPUTPOL_NONINVERTED;
			case OutputPolarity::Inverted: return LL_COMP_OUTPUTPOL_INVERTED;
		}
		SystemAbort();
		return 0;
	}


	// COMP3 is guarded by its CMSIS define: STM32G071xx has no COMP3, and comparing against an undefined name would not compile.
	static uint32 GetExtiLine(COMP_TypeDef *instance) {
		if (instance == COMP1) {
			return LL_EXTI_LINE_17;
		}

		if (instance == COMP2) {
			return LL_EXTI_LINE_18;
		}

	#if defined(COMP3)
		if (instance == COMP3) {
			return LL_EXTI_LINE_20;
		}
	#endif

		SystemAbort();
		return 0;
	}
};
