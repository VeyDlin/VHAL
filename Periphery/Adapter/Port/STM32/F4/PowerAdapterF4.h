#pragma once
#include <Adapter/PowerAdapter.h>


using APower = class PowerAdapterF4;


// STM32F4 power port: Sleep, Stop (main or low-power regulator), Standby, the WKUP pins, the reset cause from RCC_CSR and
// the PVD on EXTI line 16. Written against the PWR register map of the F4 family; the chip differences (the second and
// third WKUP pin, the single-pin EWUP bit of the older chips) are gated by CMSIS defines, so every F4 chip compiles with
// this file.
//
// BSP requirements for PowerAdapterF4:
//   - Clock: the PWR peripheral clock (RCC_APB1ENR.PWREN) must be enabled before the adapter is used. The adapter does not
//     enable it. Initialization(), EnableVoltageMonitor(), EnableWakeupPin(), DisableWakeupPin() and EnterLowPower() in
//     Stop or Standby return notReady while it is off. ClearResetCause() cannot write PWR without the clock, so it only
//     clears RCC_CSR then.
//   - Order after every start: enable PWR, read GetResetCause() and GetWakeupPin(), then call ClearResetCause(). The
//     reset flags of RCC_CSR need no clock, but the Standby flag (PWR_CSR.SBF) and the wakeup flag (PWR_CSR.WUF) are PWR
//     registers: with the clock off a standby wakeup reads as no wakeup.
//   - Clock tree: the adapter never configures RCC. Stop turns off HSE and the PLL, so EnterLowPower() returns on HSI and
//     the BSP restarts the clocks it needs before it uses any peripheral.
//   - NVIC: enable PVD_IRQn and call IrqHandler() from PVD_IRQHandler().
//   - Wakeup pins: WKUP1 is PA0 (the vendor LL header names it so; confirm it in the datasheet pinout of the chip). The
//     pin is configured as a plain input before it is armed. WKUP2 and WKUP3 exist only on the chips that define
//     PWR_CSR_EWUP2 and PWR_CSR_EWUP3; their pins are in the datasheet pinout of the chip.
//   - Stop wakeup on a pin: EnableWakeupPin() arms the pin for Standby only. Stop wakes on EXTI lines, so the BSP sets up
//     the EXTI line of the pin (rising trigger and interrupt) and its NVIC line for Stop.
//   - RTC wakeup: an RTC wakeup from Standby sets the same WUF flag as a pin. While an RTC wake flag is set in RTC_ISR,
//     GetWakeupPin() reports notAvailable. The BSP clears those flags in the RTC after it has read the wakeup cause,
//     otherwise a later pin wakeup is reported as notAvailable.
//
// Wakeup: Sleep wakes on any interrupt. Stop wakes on any enabled EXTI line (the PVD line included) and keeps RAM. Standby
// wakes through a reset, so the BSP reads GetResetCause() and GetWakeupPin() first after the reset and then calls
// ClearResetCause().


class PowerAdapterF4: public PowerAdapter<PWR_TypeDef> {
public:
	struct Mode {
		// The core waits for an interrupt; clocks and RAM keep running.
		static inline constexpr ModeOption Sleep                 { 0 };
		// Stop with the main regulator: the fastest wakeup and the highest Stop current.
		static inline constexpr ModeOption StopMainRegulator     { 1 };
		// Stop with the low-power regulator: the lower Stop current, at the cost of a slower wakeup.
		static inline constexpr ModeOption StopLowPowerRegulator { 2 };
		// RAM and registers are lost; the wakeup is a reset.
		static inline constexpr ModeOption Standby               { 3 };
	};


	struct WakeupPin {
		// WKUP1 is PA0 on the F4 family (see the BSP notes above).
		static inline constexpr WakeupPinOption WKUP1 { 1 };
#if defined(PWR_CSR_EWUP2)
		// Only the chips that define PWR_CSR_EWUP2 have this pin; its position is in the datasheet pinout of the chip.
		static inline constexpr WakeupPinOption WKUP2 { 2 };
#endif
#if defined(PWR_CSR_EWUP3)
		// Only the chips that define PWR_CSR_EWUP3 have this pin; its position is in the datasheet pinout of the chip.
		static inline constexpr WakeupPinOption WKUP3 { 3 };
#endif
	};


	struct Threshold {
		// The voltage at which the PVD output changes, from the PLS field of PWR_CR.
		static inline constexpr ThresholdOption V2_2 { LL_PWR_PVDLEVEL_0 };
		static inline constexpr ThresholdOption V2_3 { LL_PWR_PVDLEVEL_1 };
		static inline constexpr ThresholdOption V2_4 { LL_PWR_PVDLEVEL_2 };
		static inline constexpr ThresholdOption V2_5 { LL_PWR_PVDLEVEL_3 };
		static inline constexpr ThresholdOption V2_6 { LL_PWR_PVDLEVEL_4 };
		static inline constexpr ThresholdOption V2_7 { LL_PWR_PVDLEVEL_5 };
		static inline constexpr ThresholdOption V2_8 { LL_PWR_PVDLEVEL_6 };
		static inline constexpr ThresholdOption V2_9 { LL_PWR_PVDLEVEL_7 };
	};


public:
	PowerAdapterF4() = default;


	PowerAdapterF4(PWR_TypeDef *power): PowerAdapter(power) { }


	virtual ResultStatus EnterLowPower(ModeOption mode) override {
		if (mode == Mode::Sleep) {
			// SLEEPDEEP is cleared so WFI idles the core only. The write must complete before WFI.
			SCB->SCR &= ~SCB_SCR_SLEEPDEEP_Msk;
			__DSB();
			__WFI();
			return ResultStatus::ok;
		}

		uint32 pwrMode = 0U;
		if (mode == Mode::StopMainRegulator) {
			pwrMode = LL_PWR_MODE_STOP_MAINREGU;
		} else if (mode == Mode::StopLowPowerRegulator) {
			pwrMode = LL_PWR_MODE_STOP_LPREGU;
		} else if (mode == Mode::Standby) {
			pwrMode = LL_PWR_MODE_STANDBY;
		} else {
			return ResultStatus::invalidParameter;
		}

		// Checked before any PWR write: with the clock off the writes are dropped and the mode is not selected.
		if (!IsPwrClockEnabled()) {
			return ResultStatus::notReady;
		}

		if (mode == Mode::Standby) {
			// A WUF left over from an earlier wakeup would wake the chip at once, so it is cleared before entering.
			SET_BIT(powerHandle->CR, PWR_CR_CWUF);
			EnterDeepSleep(pwrMode);

			// Standby resets the chip on wakeup, so returning means WFI did not enter the mode (an interrupt was already
			// pending). The caller must not take that for a Standby entry.
			return ResultStatus::busy;
		}

		EnterDeepSleep(pwrMode);

		return ResultStatus::ok;
	}


	virtual ResultStatus EnableWakeupPin(WakeupPinOption pin, Edge edge) override {
		uint32 wakeupBit = CastWakeupPin(pin);
		if (wakeupBit == 0) {
			return ResultStatus::invalidParameter;
		}

		// The F4 wakes on the rising edge of a WKUP pin only; there is no hardware for the falling edge.
		if (edge != Edge::Rising) {
			return ResultStatus::notSupported;
		}

		if (!IsPwrClockEnabled()) {
			return ResultStatus::notReady;
		}

		// EWUP arms the pin for Standby only. The pin wakes Stop only through its EXTI line, which the BSP sets up.
		SET_BIT(powerHandle->CSR, wakeupBit);

		return ResultStatus::ok;
	}


	virtual ResultStatus DisableWakeupPin(WakeupPinOption pin) override {
		uint32 wakeupBit = CastWakeupPin(pin);
		if (wakeupBit == 0) {
			return ResultStatus::invalidParameter;
		}

		if (!IsPwrClockEnabled()) {
			return ResultStatus::notReady;
		}

		CLEAR_BIT(powerHandle->CSR, wakeupBit);

		return ResultStatus::ok;
	}


	virtual ResetCause GetResetCause() const override {
		uint32 csr = RCC->CSR;

		// The most specific cause comes first: a watchdog reset also sets the pin flag, and a standby wakeup is reported
		// before the power-on flag, which the chip may also set on that restart.
		if ((csr & RCC_CSR_LPWRRSTF) != 0) {
			return ResetCause::LowPowerFault;
		}

		if ((csr & RCC_CSR_WWDGRSTF) != 0) {
			return ResetCause::WindowWatchdog;
		}

		if ((csr & RCC_CSR_IWDGRSTF) != 0) {
			return ResetCause::IndependentWatchdog;
		}

		if ((csr & RCC_CSR_SFTRSTF) != 0) {
			return ResetCause::Software;
		}

		// SBF is a PWR flag and reads as 0 while the PWR clock is off (see the BSP notes). It is kept across resets until
		// CSBF is written, so ClearResetCause() must run after each start.
		if (IsStandbyFlagSet()) {
			return ResetCause::LowPowerWakeup;
		}

		if ((csr & RCC_CSR_BORRSTF) != 0) {
			return ResetCause::BrownOut;
		}

		if ((csr & RCC_CSR_PORRSTF) != 0) {
			return ResetCause::PowerOn;
		}

		if ((csr & RCC_CSR_PINRSTF) != 0) {
			return ResetCause::Pin;
		}

		return ResetCause::Unknown;
	}


	virtual void ClearResetCause() override {
		// RMVF is in RCC_CSR and needs no PWR clock.
		SET_BIT(RCC->CSR, RCC_CSR_RMVF);

		// CSBF and CWUF are PWR writes. With the PWR clock off they are dropped, and SBF would stay set so the next start
		// reports a standby wakeup that did not happen. The BSP enables PWR before this call (see the BSP notes).
		if (IsPwrClockEnabled()) {
			SET_BIT(powerHandle->CR, PWR_CR_CSBF | PWR_CR_CWUF);
		}
	}


	virtual Result<WakeupPinOption> GetWakeupPin() const override {
		if (!IsStandbyFlagSet() || READ_BIT(powerHandle->CSR, PWR_CSR_WUF) == 0) {
			return ResultStatus::notAvailable;
		}

		// WUF is also set by the RTC wakeup from Standby. An RTC wake flag shows that the RTC was the source, so no pin is
		// named then.
		if (IsRtcWakeupFlagSet()) {
			return ResultStatus::notAvailable;
		}

		// The shared flag cannot tell the pins apart, so a pin is named only when exactly one pin is armed.
		uint8 armedCount = 0;
		WakeupPinOption armedPin = WakeupPin::WKUP1;

		if (IsWakeupPinEnabled(LL_PWR_WAKEUP_PIN1)) {
			armedCount++;
		}
#if defined(PWR_CSR_EWUP2)
		if (IsWakeupPinEnabled(LL_PWR_WAKEUP_PIN2)) {
			armedCount++;
			armedPin = WakeupPin::WKUP2;
		}
#endif
#if defined(PWR_CSR_EWUP3)
		if (IsWakeupPinEnabled(LL_PWR_WAKEUP_PIN3)) {
			armedCount++;
			armedPin = WakeupPin::WKUP3;
		}
#endif

		if (armedCount != 1) {
			return ResultStatus::notAvailable;
		}

		return armedPin;
	}


	virtual ResultStatus EnableVoltageMonitor(ThresholdOption threshold) override {
		uint32 level = threshold.Get();
		if ((level & ~PWR_CR_PLS) != 0) {
			return ResultStatus::invalidParameter;
		}

		if (!IsPwrClockEnabled()) {
			return ResultStatus::notReady;
		}

		// The PVD is switched off while the level and the EXTI line are set up. With PVDE off no edge is produced, so the
		// level change itself does not raise the interrupt.
		CLEAR_BIT(powerHandle->CR, PWR_CR_PVDE);
		MODIFY_REG(powerHandle->CR, PWR_CR_PLS, level);

		// The EXTI line is armed before PVDE is set, so a crossing right after the monitor starts is not lost.
		LL_EXTI_ClearFlag_0_31(LL_EXTI_LINE_16);
		LL_EXTI_EnableRisingTrig_0_31(LL_EXTI_LINE_16);
		LL_EXTI_EnableFallingTrig_0_31(LL_EXTI_LINE_16);
		LL_EXTI_EnableIT_0_31(LL_EXTI_LINE_16);

		SET_BIT(powerHandle->CR, PWR_CR_PVDE);

		return ResultStatus::ok;
	}


	virtual ResultStatus DisableVoltageMonitor() override {
		LL_EXTI_DisableIT_0_31(LL_EXTI_LINE_16);
		LL_EXTI_DisableRisingTrig_0_31(LL_EXTI_LINE_16);
		LL_EXTI_DisableFallingTrig_0_31(LL_EXTI_LINE_16);
		LL_EXTI_ClearFlag_0_31(LL_EXTI_LINE_16);

		CLEAR_BIT(powerHandle->CR, PWR_CR_PVDE);

		return ResultStatus::ok;
	}


	virtual bool IsVoltageLow() const override {
		if (READ_BIT(powerHandle->CR, PWR_CR_PVDE) == 0) {
			return false;
		}

		// PVDO is set while VDD is below the threshold.
		return READ_BIT(powerHandle->CSR, PWR_CSR_PVDO) != 0;
	}


	virtual inline void IrqHandler() override {
		if (LL_EXTI_IsActiveFlag_0_31(LL_EXTI_LINE_16) == 0) {
			return;
		}

		// The pending bit is cleared before PVDO is read: a crossing during the callback then raises a new interrupt
		// instead of being lost.
		LL_EXTI_ClearFlag_0_31(LL_EXTI_LINE_16);
		CallVoltageChange(IsVoltageLow());
	}


protected:
	virtual ResultStatus Initialization() override {
		ResultStatus status = BeforeInitialization();
		if (status != ResultStatus::ok) {
			return status;
		}

		if (!IsPwrClockEnabled()) {
			return ResultStatus::notReady;
		}

		return AfterInitialization();
	}


private:
	bool IsPwrClockEnabled() const {
		return LL_APB1_GRP1_IsEnabledClock(LL_APB1_GRP1_PERIPH_PWR) != 0;
	}


	bool IsStandbyFlagSet() const {
		return READ_BIT(powerHandle->CSR, PWR_CSR_SBF) != 0;
	}


	bool IsWakeupPinEnabled(uint32 wakeupBit) const {
		return READ_BIT(powerHandle->CSR, wakeupBit) != 0;
	}


	// The RTC wake sources set their flags in RTC_ISR. The flags are kept until the BSP clears them in the RTC.
	bool IsRtcWakeupFlagSet() const {
		uint32 flags = RTC_ISR_ALRAF | RTC_ISR_ALRBF | RTC_ISR_WUTF | RTC_ISR_TSF;
#if defined(RTC_ISR_TAMP1F)
		flags |= RTC_ISR_TAMP1F;
#endif
#if defined(RTC_ISR_TAMP2F)
		flags |= RTC_ISR_TAMP2F;
#endif

		return (RTC->ISR & flags) != 0;
	}


	// PDDS and LPDS pick the mode and SLEEPDEEP makes WFI enter it. Both are cleared after WFI returns, so a later Sleep is
	// plain and a Standby that did not happen leaves no mode selected.
	void EnterDeepSleep(uint32 pwrMode) {
		MODIFY_REG(powerHandle->CR, PWR_CR_PDDS | PWR_CR_LPDS, pwrMode);
		SCB->SCR |= SCB_SCR_SLEEPDEEP_Msk;

		// The PWR and SCB writes must complete before WFI, or the core can enter the mode that was selected before.
		__DSB();
		__WFI();

		MODIFY_REG(powerHandle->CR, PWR_CR_PDDS | PWR_CR_LPDS, 0U);
		SCB->SCR &= ~SCB_SCR_SLEEPDEEP_Msk;
	}


	static uint32 CastWakeupPin(WakeupPinOption pin) {
		if (pin == WakeupPin::WKUP1) {
			return LL_PWR_WAKEUP_PIN1;
		}
#if defined(PWR_CSR_EWUP2)
		if (pin == WakeupPin::WKUP2) {
			return LL_PWR_WAKEUP_PIN2;
		}
#endif
#if defined(PWR_CSR_EWUP3)
		if (pin == WakeupPin::WKUP3) {
			return LL_PWR_WAKEUP_PIN3;
		}
#endif
		return 0;
	}
};
