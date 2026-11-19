#pragma once
#include <Adapter/PowerAdapter.h>


// BSP requirements for PowerAdapterG0:
//   - Clock: the PWR peripheral clock (RCC_APBENR1.PWREN) must be enabled before the adapter is used. The adapter does
//     not enable it. Initialization(), EnterLowPower() in the non-sleep modes, EnableWakeupPin(), DisableWakeupPin() and
//     EnableVoltageMonitor() return notReady while it is off. DisableVoltageMonitor() and ClearResetCause() write PWR
//     without the check, so they do nothing while it is off. GetWakeupPin() and IsVoltageLow() read PWR without the check
//     and see no flags, so they return notAvailable and false.
//   - Clock tree: the adapter never configures RCC. After EnterLowPower() returns from Stop0 or Stop1 the core runs on
//     HSI16, so the BSP restores the system clock tree and the clocks of its peripherals.
//   - Wakeup pins: the GPIO of each wakeup pin (WKUP1..WKUP6, the ones the chip has) is configured as an input by the
//     BSP. The pin-to-GPIO mapping is chip-specific and is listed in the reference manual, not in this adapter.
//   - NVIC: enable the PVD interrupt and call IrqHandler() of this adapter from its handler. The vector name differs by
//     chip: PVD_VDDIO2_IRQn on G0B1 and G0C1, PVD_IRQn on G031, G041 and G071. The adapter clears the EXTI line 16 flag
//     itself.
//   - VDDIO2 monitor (G0B1 and G0C1 only): the monitor shares PVD_VDDIO2_IRQn through EXTI line 34, and this adapter does
//     not handle that line. The BSP must not enable the VDDIO2 monitor unless its own handler clears the line 34 flags on
//     each call (LL_EXTI_ClearRisingFlag_32_63 and LL_EXTI_ClearFallingFlag_32_63 for LL_EXTI_LINE_34). A flag left
//     pending keeps the shared vector pending, and the core stays in the ISR.
//   - Voltage monitor: ThresholdOption is an index into a table of falling and rising PVD levels (see Threshold). The PVD
//     monitors VDD; the VDDIO2 and VDDA monitors are not handled here.


using APower = class PowerAdapterG0;


class PowerAdapterG0: public PowerAdapter<PWR_TypeDef> {
public:
	// Low-power modes in order of depth. Sleep keeps the clocks running; Stop0 and Stop1 keep RAM and registers, the
	// others lose RAM. Sleep has no LPMS code, so its value only has to stay clear of the LPMS codes.
	struct Mode {
		static inline constexpr ModeOption Sleep    { 0xFF };
		static inline constexpr ModeOption Stop0    { LL_PWR_MODE_STOP0 };
		static inline constexpr ModeOption Stop1    { LL_PWR_MODE_STOP1 };
		static inline constexpr ModeOption Standby  { LL_PWR_MODE_STANDBY };
		#if defined(PWR_CR1_LPMS_2)
		static inline constexpr ModeOption Shutdown { LL_PWR_MODE_SHUTDOWN };
		#endif
	};


	// The chip's dedicated wakeup inputs. Only the pins the chip has are named: G031 has no WKUP3 and WKUP5, G071 has no
	// WKUP3.
	struct WakeupPin {
		#if defined(PWR_CR3_EWUP1)
		static inline constexpr WakeupPinOption WKUP1 { 1 };
		#endif
		#if defined(PWR_CR3_EWUP2)
		static inline constexpr WakeupPinOption WKUP2 { 2 };
		#endif
		#if defined(PWR_CR3_EWUP3)
		static inline constexpr WakeupPinOption WKUP3 { 3 };
		#endif
		#if defined(PWR_CR3_EWUP4)
		static inline constexpr WakeupPinOption WKUP4 { 4 };
		#endif
		#if defined(PWR_CR3_EWUP5)
		static inline constexpr WakeupPinOption WKUP5 { 5 };
		#endif
		#if defined(PWR_CR3_EWUP6)
		static inline constexpr WakeupPinOption WKUP6 { 6 };
		#endif
	};


	// The PVD has a falling level (PVDLS) and a rising level (PVDHS), and the rising one must sit above the falling one or
	// the output chatters. Each option picks a falling level and the rising level 0.1 V above it, so the hysteresis is the
	// same for every option. The falling level is in the name.
	struct Threshold {
		#if defined(PWR_CR2_PVDE)
		static inline constexpr ThresholdOption V2_05 { 0 };
		static inline constexpr ThresholdOption V2_20 { 1 };
		static inline constexpr ThresholdOption V2_36 { 2 };
		static inline constexpr ThresholdOption V2_52 { 3 };
		static inline constexpr ThresholdOption V2_64 { 4 };
		static inline constexpr ThresholdOption V2_81 { 5 };
		static inline constexpr ThresholdOption V2_91 { 6 };
		#endif
	};


private:
	#if defined(PWR_CR2_PVDE)
	static constexpr uint8 thresholdCount = 7;
	static constexpr uint32 fallingLevelArray[thresholdCount] = {
		LL_PWR_PVDLLEVEL_0, LL_PWR_PVDLLEVEL_1, LL_PWR_PVDLLEVEL_2, LL_PWR_PVDLLEVEL_3,
		LL_PWR_PVDLLEVEL_4, LL_PWR_PVDLLEVEL_5, LL_PWR_PVDLLEVEL_6
	};
	static constexpr uint32 risingLevelArray[thresholdCount] = {
		LL_PWR_PVDHLEVEL_0, LL_PWR_PVDHLEVEL_1, LL_PWR_PVDHLEVEL_2, LL_PWR_PVDHLEVEL_3,
		LL_PWR_PVDHLEVEL_4, LL_PWR_PVDHLEVEL_5, LL_PWR_PVDHLEVEL_6
	};
	#endif


public:
	PowerAdapterG0() = default;
	PowerAdapterG0(PWR_TypeDef *power):PowerAdapter(power) { }


	virtual ResultStatus EnterLowPower(ModeOption mode) override {
		if (mode == Mode::Sleep) {
			LL_LPM_EnableSleep();
			__DSB();
			__WFI();
			return ResultStatus::ok;
		}

		uint32 powerMode = LL_PWR_MODE_STOP0;
		if (mode == Mode::Stop0) {
			powerMode = LL_PWR_MODE_STOP0;
		} else if (mode == Mode::Stop1) {
			powerMode = LL_PWR_MODE_STOP1;
		} else if (mode == Mode::Standby) {
			powerMode = LL_PWR_MODE_STANDBY;
		#if defined(PWR_CR1_LPMS_2)
		} else if (mode == Mode::Shutdown) {
			powerMode = LL_PWR_MODE_SHUTDOWN;
		#endif
		} else {
			return ResultStatus::invalidParameter;
		}

		if (!IsPowerClockEnabled()) {
			return ResultStatus::notReady;
		}

		// A wakeup flag left from an earlier wakeup would look like a new one after this entry, so the flags are cleared
		// before the chip goes down.
		LL_PWR_ClearFlag_SB();
		LL_PWR_ClearFlag_WU();

		// The mode is selected in PWR_CR1 and the core then goes to deep sleep. The write must complete before WFI, or the
		// core can stop in the mode that was selected before.
		LL_PWR_SetPowerMode(powerMode);
		LL_LPM_EnableDeepSleep();
		__DSB();
		__WFI();

		// Restore the plain sleep setting so a later WFI in sleep does not stop the clocks by mistake.
		LL_LPM_EnableSleep();

		// Standby and Shutdown reset the chip on wakeup, so any return from them means the core did not enter the mode (a
		// pending interrupt makes WFI return at once), and the port reports busy.
		if (powerMode == LL_PWR_MODE_STANDBY) {
			return ResultStatus::busy;
		}
		#if defined(PWR_CR1_LPMS_2)
		if (powerMode == LL_PWR_MODE_SHUTDOWN) {
			return ResultStatus::busy;
		}
		#endif

		// Stop0 and Stop1 return ok on every return from WFI. A pending interrupt can make WFI return before the core stops,
		// and the port cannot tell that from a real wakeup, so for the Stop modes an early return counts as a wakeup.
		return ResultStatus::ok;
	}


	virtual ResultStatus EnableWakeupPin(WakeupPinOption pin, Edge edge) override {
		uint32 mask = GetWakeupMask(pin);
		if (mask == 0) {
			return ResultStatus::invalidParameter;
		}

		if (!IsPowerClockEnabled()) {
			return ResultStatus::notReady;
		}

		// The polarity bit is set for a low level and cleared for a high level, so the edge is picked with the polarity.
		if (edge == Edge::Rising) {
			LL_PWR_SetWakeUpPinPolarityHigh(mask);
		} else {
			LL_PWR_SetWakeUpPinPolarityLow(mask);
		}

		LL_PWR_EnableWakeUpPin(mask);

		return ResultStatus::ok;
	}


	virtual ResultStatus DisableWakeupPin(WakeupPinOption pin) override {
		uint32 mask = GetWakeupMask(pin);
		if (mask == 0) {
			return ResultStatus::invalidParameter;
		}

		if (!IsPowerClockEnabled()) {
			return ResultStatus::notReady;
		}

		LL_PWR_DisableWakeUpPin(mask);

		return ResultStatus::ok;
	}


	// The most specific cause comes first. A watchdog or a low-power fault also sets the pin flag on STM32, so the pin
	// flag is checked last. The standby flag is checked after the reset flags because a wakeup from standby sets it on
	// its own. RCC_CSR has no bit that separates a brown-out from a power-on reset: PWRRSTF covers both, so the port
	// reports it as PowerOn. A brown-out is only visible through the voltage monitor.
	virtual ResetCause GetResetCause() const override {
		if (LL_RCC_IsActiveFlag_LPWRRST() != 0) {
			return ResetCause::LowPowerFault;
		}

		if (LL_RCC_IsActiveFlag_WWDGRST() != 0) {
			return ResetCause::WindowWatchdog;
		}

		if (LL_RCC_IsActiveFlag_IWDGRST() != 0) {
			return ResetCause::IndependentWatchdog;
		}

		if (LL_RCC_IsActiveFlag_SFTRST() != 0) {
			return ResetCause::Software;
		}

		if (LL_RCC_IsActiveFlag_OBLRST() != 0) {
			return ResetCause::OptionLoad;
		}

		if (LL_PWR_IsActiveFlag_SB() != 0) {
			return ResetCause::LowPowerWakeup;
		}

		if (LL_RCC_IsActiveFlag_PINRST() != 0) {
			return ResetCause::Pin;
		}

		if (LL_RCC_IsActiveFlag_PWRRST() != 0) {
			return ResetCause::PowerOn;
		}

		return ResetCause::Unknown;
	}


	// Clears the reset flags and, when the PWR clock is on, the standby and wakeup flags. The PWR part is a no-op while the
	// clock is off, so the BSP calls this only after Initialization() has succeeded. The standby flag survives a reset, so
	// GetWakeupPin() must be read before this call.
	virtual void ClearResetCause() override {
		LL_RCC_ClearResetFlags();

		if (IsPowerClockEnabled()) {
			LL_PWR_ClearFlag_SB();
			LL_PWR_ClearFlag_WU();
		}
	}


	// Standby and Shutdown set the standby flag; a wakeup from another source has no pin flag, so notAvailable.
	virtual Result<WakeupPinOption> GetWakeupPin() const override {
		if (LL_PWR_IsActiveFlag_SB() == 0) {
			return ResultStatus::notAvailable;
		}

		#if defined(PWR_CR3_EWUP1)
		if (LL_PWR_IsActiveFlag_WU1() != 0) {
			return WakeupPin::WKUP1;
		}
		#endif

		#if defined(PWR_CR3_EWUP2)
		if (LL_PWR_IsActiveFlag_WU2() != 0) {
			return WakeupPin::WKUP2;
		}
		#endif

		#if defined(PWR_CR3_EWUP3)
		if (LL_PWR_IsActiveFlag_WU3() != 0) {
			return WakeupPin::WKUP3;
		}
		#endif

		#if defined(PWR_CR3_EWUP4)
		if (LL_PWR_IsActiveFlag_WU4() != 0) {
			return WakeupPin::WKUP4;
		}
		#endif

		#if defined(PWR_CR3_EWUP5)
		if (LL_PWR_IsActiveFlag_WU5() != 0) {
			return WakeupPin::WKUP5;
		}
		#endif

		#if defined(PWR_CR3_EWUP6)
		if (LL_PWR_IsActiveFlag_WU6() != 0) {
			return WakeupPin::WKUP6;
		}
		#endif

		return ResultStatus::notAvailable;
	}


	// The PVD output is routed to EXTI line 16, so both edges raise the interrupt and IrqHandler() reads the level.
	virtual ResultStatus EnableVoltageMonitor(ThresholdOption threshold) override {
		#if defined(PWR_CR2_PVDE)
			uint8 index = threshold.Get();
			if (index >= thresholdCount) {
				return ResultStatus::invalidParameter;
			}

			if (!IsPowerClockEnabled()) {
				return ResultStatus::notReady;
			}

			// The PVD and its EXTI line are switched off while the levels change. A crossing during the change then cannot
			// raise the interrupt, and an interrupt left from an earlier start cannot fire on the new levels before the flags
			// are cleared.
			LL_PWR_DisablePVD();
			LL_EXTI_DisableIT_0_31(LL_EXTI_LINE_16);

			LL_PWR_SetPVDLowLevel(fallingLevelArray[index]);
			LL_PWR_SetPVDHighLevel(risingLevelArray[index]);

			LL_EXTI_ClearRisingFlag_0_31(LL_EXTI_LINE_16);
			LL_EXTI_ClearFallingFlag_0_31(LL_EXTI_LINE_16);
			LL_EXTI_EnableRisingTrig_0_31(LL_EXTI_LINE_16);
			LL_EXTI_EnableFallingTrig_0_31(LL_EXTI_LINE_16);
			LL_EXTI_EnableIT_0_31(LL_EXTI_LINE_16);

			LL_PWR_EnablePVD();

			return ResultStatus::ok;
		#else
			return ResultStatus::notSupported;
		#endif
	}


	virtual ResultStatus DisableVoltageMonitor() override {
		#if defined(PWR_CR2_PVDE)
			LL_PWR_DisablePVD();

			LL_EXTI_DisableIT_0_31(LL_EXTI_LINE_16);
			LL_EXTI_DisableRisingTrig_0_31(LL_EXTI_LINE_16);
			LL_EXTI_DisableFallingTrig_0_31(LL_EXTI_LINE_16);
			LL_EXTI_ClearRisingFlag_0_31(LL_EXTI_LINE_16);
			LL_EXTI_ClearFallingFlag_0_31(LL_EXTI_LINE_16);

			return ResultStatus::ok;
		#else
			return ResultStatus::notSupported;
		#endif
	}


	// The PVDO bit is only meaningful while the monitor runs, so a stopped monitor reports false.
	virtual bool IsVoltageLow() const override {
		#if defined(PWR_CR2_PVDE)
			if (LL_PWR_IsEnabledPVD() == 0) {
				return false;
			}

			return LL_PWR_IsActiveFlag_PVDO() != 0;
		#else
			return false;
		#endif
	}


	virtual void IrqHandler() override {
		#if defined(PWR_CR2_PVDE)
			uint32 risingPending = LL_EXTI_IsActiveRisingFlag_0_31(LL_EXTI_LINE_16);
			uint32 fallingPending = LL_EXTI_IsActiveFallingFlag_0_31(LL_EXTI_LINE_16);
			if (risingPending == 0 && fallingPending == 0) {
				return;
			}

			LL_EXTI_ClearRisingFlag_0_31(LL_EXTI_LINE_16);
			LL_EXTI_ClearFallingFlag_0_31(LL_EXTI_LINE_16);
			CallVoltageChange(IsVoltageLow());
		#endif
	}


protected:
	virtual ResultStatus Initialization() override {
		ResultStatus status = BeforeInitialization();
		if (status != ResultStatus::ok) {
			return status;
		}

		if (!IsPowerClockEnabled()) {
			return ResultStatus::notReady;
		}

		return AfterInitialization();
	}


private:
	bool IsPowerClockEnabled() const {
		return LL_APB1_GRP1_IsEnabledClock(LL_APB1_GRP1_PERIPH_PWR) != 0;
	}


	// Returns 0 for a pin the chip does not have, which the callers turn into invalidParameter.
	static constexpr uint32 GetWakeupMask(WakeupPinOption pin) {
		#if defined(PWR_CR3_EWUP1)
		if (pin == WakeupPin::WKUP1) {
			return LL_PWR_WAKEUP_PIN1;
		}
		#endif

		#if defined(PWR_CR3_EWUP2)
		if (pin == WakeupPin::WKUP2) {
			return LL_PWR_WAKEUP_PIN2;
		}
		#endif

		#if defined(PWR_CR3_EWUP3)
		if (pin == WakeupPin::WKUP3) {
			return LL_PWR_WAKEUP_PIN3;
		}
		#endif

		#if defined(PWR_CR3_EWUP4)
		if (pin == WakeupPin::WKUP4) {
			return LL_PWR_WAKEUP_PIN4;
		}
		#endif

		#if defined(PWR_CR3_EWUP5)
		if (pin == WakeupPin::WKUP5) {
			return LL_PWR_WAKEUP_PIN5;
		}
		#endif

		#if defined(PWR_CR3_EWUP6)
		if (pin == WakeupPin::WKUP6) {
			return LL_PWR_WAKEUP_PIN6;
		}
		#endif

		return 0;
	}
};
