#pragma once
#include <Adapter/PowerAdapter.h>


// BSP requirements for PowerAdapterG4:
//   - Clock: the PWR peripheral clock (RCC_APB1ENR1.PWREN) must be enabled before the adapter is used. The adapter does
//     not enable it; Initialization() returns notReady while it is off, and the calls that touch PWR return notReady.
//   - Clock tree: the adapter never configures RCC. After EnterLowPower() returns from Stop0 or Stop1 the core runs on
//     HSI16, so the BSP restores the system clock tree and the clocks of its peripherals.
//   - Wakeup pins: the GPIO of each wakeup pin (WKUP1..5) is configured as an input by the BSP. The pin-to-GPIO mapping
//     is chip-specific and is listed in the reference manual, not in this adapter.
//   - NVIC: enable PVD_PVM_IRQn and call IrqHandler() of this adapter from PVD_PVM_IRQHandler(). The adapter clears the
//     EXTI line 16 flag itself; the vector is shared with PVM1..4, which this adapter does not handle.
//   - PVD_IN pin: when Threshold::ExternalInput is used, the BSP configures the PVD_IN GPIO (chip-specific, listed in the
//     reference manual) as an analog input before EnableVoltageMonitor().
//   - UCPD dead battery: after reset the UCPD CC pins carry the dead-battery pull-downs until PWR_CR3.UCPD_DBDIS is set.
//     A BSP that uses USB Type-C or Power Delivery calls LL_PWR_DisableUCPDDeadBattery() early in boot, before it
//     enables UCPD; otherwise the CC pins stay pulled down and the source does not negotiate.


using APOWER = class PowerAdapterG4;


class PowerAdapterG4: public PowerAdapter<PWR_TypeDef> {
public:
	// Low-power modes in order of depth. Sleep keeps the clocks running; Stop0 and Stop1 keep RAM and registers, the
	// others lose RAM.
	struct Mode {
		// Not an LPMS value: sleep leaves the PWR mode field alone, so its code must differ from Stop0 (which is 0).
		static inline constexpr ModeOption Sleep    { 0x10 };
		static inline constexpr ModeOption Stop0    { LL_PWR_MODE_STOP0 };
		static inline constexpr ModeOption Stop1    { LL_PWR_MODE_STOP1 };
		static inline constexpr ModeOption Standby  { LL_PWR_MODE_STANDBY };
		static inline constexpr ModeOption Shutdown { LL_PWR_MODE_SHUTDOWN };
	};


	// The chip's dedicated wakeup inputs. Only the pins the chip has are named.
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
	};


	// PVD thresholds. ExternalInput compares the PVD_IN pin with the internal reference instead of a fixed level.
	struct Threshold {
		static inline constexpr ThresholdOption Level2V0      { LL_PWR_PVDLEVEL_0 };
		static inline constexpr ThresholdOption Level2V2      { LL_PWR_PVDLEVEL_1 };
		static inline constexpr ThresholdOption Level2V4      { LL_PWR_PVDLEVEL_2 };
		static inline constexpr ThresholdOption Level2V5      { LL_PWR_PVDLEVEL_3 };
		static inline constexpr ThresholdOption Level2V6      { LL_PWR_PVDLEVEL_4 };
		static inline constexpr ThresholdOption Level2V8      { LL_PWR_PVDLEVEL_5 };
		static inline constexpr ThresholdOption Level2V9      { LL_PWR_PVDLEVEL_6 };
		static inline constexpr ThresholdOption ExternalInput { LL_PWR_PVDLEVEL_7 };
	};


public:
	PowerAdapterG4() = default;
	PowerAdapterG4(PWR_TypeDef *power):PowerAdapter(power) { }


	virtual ResultStatus EnterLowPower(ModeOption mode) override {
		uint32 powerMode = LL_PWR_MODE_STOP0;
		bool deepSleep = true;

		if (mode == Mode::Sleep) {
			deepSleep = false;
		} else if (mode == Mode::Stop0) {
			powerMode = LL_PWR_MODE_STOP0;
		} else if (mode == Mode::Stop1) {
			powerMode = LL_PWR_MODE_STOP1;
		} else if (mode == Mode::Standby) {
			powerMode = LL_PWR_MODE_STANDBY;
		} else if (mode == Mode::Shutdown) {
			powerMode = LL_PWR_MODE_SHUTDOWN;
		} else {
			return ResultStatus::invalidParameter;
		}

		// Sleep never touches PWR, so only the deep modes need the PWR clock.
		if (!deepSleep) {
			LL_LPM_EnableSleep();

			__DSB();
			__WFI();

			return ResultStatus::ok;
		}

		if (!IsPowerClockEnabled()) {
			return ResultStatus::notReady;
		}

		// A wakeup flag left from an earlier wakeup would look like a new one after this entry, so the flags are cleared
		// before the chip goes down.
		LL_PWR_ClearFlag_SB();
		LL_PWR_ClearFlag_WU();

		LL_PWR_SetPowerMode(powerMode);
		LL_LPM_EnableDeepSleep();

		// The write to the PWR control register must complete before the core stops.
		__DSB();
		__WFI();

		// Restore the plain sleep setting so a later WFI in sleep does not stop the clocks by mistake.
		LL_LPM_EnableSleep();

		// Stop0 and Stop1 return on wakeup. Standby and Shutdown reset the chip on wakeup, so reaching this point with those
		// modes means a pending interrupt woke the core before the mode was entered: the chip did not sleep.
		if (powerMode == LL_PWR_MODE_STANDBY) {
			return ResultStatus::busy;
		}

		if (powerMode == LL_PWR_MODE_SHUTDOWN) {
			return ResultStatus::busy;
		}

		return ResultStatus::ok;
	}


	virtual ResultStatus EnableWakeupPin(WakeupPinOption pin, Edge edge) override {
		uint32 mask = GetWakeupMask(pin);
		if (mask == 0) {
			return ResultStatus::invalidParameter;
		}

		// With the PWR clock gated the register writes are lost, so the call must not report success.
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


	// The most specific cause comes first. The pin flag is checked after the more specific flags, so a reset that sets
	// it together with another flag reports the more specific cause. The wakeup from standby is checked from PWR_SR1
	// because standby leaves it set after the reset.
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

		// RCC_CSR has no bit that tells a power-on from a brown-out: BORRSTF is set by the power-on reset circuit as well
		// as by a brown-out. The port reports the flag as PowerOn, as the G0 port does, so a cold boot is not reported as
		// a brown-out. On G4 BrownOut is therefore never returned; a brown-out is visible only through the voltage monitor.
		if (LL_RCC_IsActiveFlag_BORRST() != 0) {
			return ResetCause::PowerOn;
		}

		return ResetCause::Unknown;
	}


	virtual void ClearResetCause() override {
		LL_RCC_ClearResetFlags();

		if (IsPowerClockEnabled()) {
			LL_PWR_ClearFlag_SB();
			LL_PWR_ClearFlag_WU();
		}
	}


	// Standby sets the standby flag; a wakeup from another source has no flag to report, so notAvailable.
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

		return ResultStatus::notAvailable;
	}


	// The PVD output is routed to EXTI line 16, so both edges raise the interrupt and IrqHandler() reads the level.
	virtual ResultStatus EnableVoltageMonitor(ThresholdOption threshold) override {
		if (!IsPowerClockEnabled()) {
			return ResultStatus::notReady;
		}

		// The level is changed with the PVD off and the interrupt masked, so the switch itself does not produce an edge.
		LL_EXTI_DisableIT_0_31(LL_EXTI_LINE_16);
		LL_PWR_DisablePVD();
		LL_PWR_SetPVDLevel(threshold.Get());

		LL_EXTI_EnableRisingTrig_0_31(LL_EXTI_LINE_16);
		LL_EXTI_EnableFallingTrig_0_31(LL_EXTI_LINE_16);
		LL_EXTI_ClearFlag_0_31(LL_EXTI_LINE_16);

		LL_PWR_EnablePVD();

		// Enabling the PVD can change its output, so the flag set by that change is cleared before the interrupt is unmasked.
		LL_EXTI_ClearFlag_0_31(LL_EXTI_LINE_16);
		LL_EXTI_EnableIT_0_31(LL_EXTI_LINE_16);

		return ResultStatus::ok;
	}


	virtual ResultStatus DisableVoltageMonitor() override {
		LL_PWR_DisablePVD();

		LL_EXTI_DisableIT_0_31(LL_EXTI_LINE_16);
		LL_EXTI_DisableRisingTrig_0_31(LL_EXTI_LINE_16);
		LL_EXTI_DisableFallingTrig_0_31(LL_EXTI_LINE_16);
		LL_EXTI_ClearFlag_0_31(LL_EXTI_LINE_16);

		return ResultStatus::ok;
	}


	// The PVDO bit is only meaningful while the monitor runs, so a stopped monitor reports false.
	virtual bool IsVoltageLow() const override {
		if (LL_PWR_IsEnabledPVD() == 0) {
			return false;
		}

		return LL_PWR_IsActiveFlag_PVDO() != 0;
	}


	virtual void IrqHandler() override {
		if (LL_EXTI_IsActiveFlag_0_31(LL_EXTI_LINE_16) == 0) {
			return;
		}

		LL_EXTI_ClearFlag_0_31(LL_EXTI_LINE_16);
		CallVoltageChange(IsVoltageLow());
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

		return 0;
	}
};
