#pragma once
#include <Adapter/OPAMPAdapter.h>


// BSP responsibilities for this port. The adapter never configures the RCC clock tree or the NVIC.
//   - Clock: on STM32G4 the OPAMP registers sit on the SYSCFG bus clock. The BSP enables
//     RCC_APB2ENR.SYSCFGEN (LL_APB2_GRP1_PERIPH_SYSCFG) before any call; without it every
//     write returns notReady instead of touching a clock-gated register.
//   - Clock source: none beyond the bus clock. The OPAMP has no kernel clock of its own.
//   - Pins: put every VINP, VINM and output pin that the application uses in analog mode
//     (GPIO MODE_ANALOG). The pin behind each VINP/VINM code per instance is listed in the comment above InputPlus (lines 22-29).
//   - Interrupts: the OPAMP has no interrupt line, so there is no IrqHandler and no vector to forward.
//   - Delay: Enable() and Calibrate() use System::DelayUs / System::DelayMs, so the system delay must be running.
//   - Instances: pass OPAMP1 .. OPAMP3 on STM32G431, OPAMP1 .. OPAMP6 on STM32G474; the CMSIS device header defines which exist.


using AOPAMP = class OPAMPAdapterG4;


class OPAMPAdapterG4: public OPAMPAdapter<OPAMP_TypeDef> {
public:
	// Pins per instance, OPAMP1 / OPAMP2 / OPAMP3 / OPAMP4 / OPAMP5 / OPAMP6, as listed in stm32g4xx_ll_opamp.h (RM0440 pinout):
	//   VINP0: PA1  / PA7  / PB0  / PB13 / PB14 / PB12
	//   VINP1: PA3  / PB14 / PB13 / PD11 / PD12 / PD9
	//   VINP2: PA7  / PB0  / PA1  / PB11 / PC3  / PB13
	//   VINP3: -    / PD14 / -    / -    / -    / -
	//   VINM0: PA3  / PA5  / PB2  / PB10 / PB15 / PA1
	//   VINM1: PC5  / PC5  / PB10 / PB8  / PA3  / PB1
	// Not every instance exists on every device, and the pinout is the datasheet's to confirm for the exact part.
	struct InputPlus {
		static inline constexpr InputPlusOption Io0 		{ LL_OPAMP_INPUT_NONINVERT_IO0 };
		static inline constexpr InputPlusOption Io1 		{ LL_OPAMP_INPUT_NONINVERT_IO1 };
		static inline constexpr InputPlusOption Io2 		{ LL_OPAMP_INPUT_NONINVERT_IO2 };
		// VPSEL code 3 selects the internal DAC on OPAMP1, OPAMP3, OPAMP4, OPAMP5 and OPAMP6
		// (DAC3_CH1, DAC3_CH2, DAC4_CH1, DAC4_CH2, DAC3_CH1), but VINP3 (PD14) on OPAMP2.
		// The register code is shared, so pick Dac or Io3 by the instance you construct.
		static inline constexpr InputPlusOption Dac 		{ LL_OPAMP_INPUT_NONINVERT_DAC };
		static inline constexpr InputPlusOption Io3 		{ LL_OPAMP_INPUT_NONINVERT_IO3 };
	};


	struct InputMinus {
		static inline constexpr InputMinusOption Io0 		{ LL_OPAMP_INPUT_INVERT_IO0 };
		static inline constexpr InputMinusOption Io1 		{ LL_OPAMP_INPUT_INVERT_IO1 };
		// Inverting input not connected to a pin. Valid for the follower and for non-inverting PGA gains only.
		static inline constexpr InputMinusOption NotConnected 	{ LL_OPAMP_INPUT_INVERT_CONNECT_NO };
	};


	struct Gain {
		// Bit 0 lies outside the PGGAIN field and marks an inverting gain. The sign of a PGA gain
		// comes from which inverting pin VMSEL connects, not from PGGAIN, so the same code serves
		// both signs and the flag has to travel inside the option. It is never written to CSR.
		static inline constexpr uint32 InvertingFlag 		{ 0x00000001UL };

		// Non-inverting gains: the inverting input stays inside the chip.
		static inline constexpr GainOption Gain2 		{ LL_OPAMP_PGA_GAIN_2_OR_MINUS_1 };
		static inline constexpr GainOption Gain4 		{ LL_OPAMP_PGA_GAIN_4_OR_MINUS_3 };
		static inline constexpr GainOption Gain8 		{ LL_OPAMP_PGA_GAIN_8_OR_MINUS_7 };
		static inline constexpr GainOption Gain16 		{ LL_OPAMP_PGA_GAIN_16_OR_MINUS_15 };
		static inline constexpr GainOption Gain32 		{ LL_OPAMP_PGA_GAIN_32_OR_MINUS_31 };
		static inline constexpr GainOption Gain64 		{ LL_OPAMP_PGA_GAIN_64_OR_MINUS_63 };

		// Inverting gains: the same PGGAIN code as the non-inverting gain of the same magnitude,
		// with VMSEL connecting VINM0 or VINM1 (InputMinus::Io0 or Io1 in Parameters).
		static inline constexpr GainOption InvertingGain1 	{ LL_OPAMP_PGA_GAIN_2_OR_MINUS_1 | InvertingFlag };
		static inline constexpr GainOption InvertingGain3 	{ LL_OPAMP_PGA_GAIN_4_OR_MINUS_3 | InvertingFlag };
		static inline constexpr GainOption InvertingGain7 	{ LL_OPAMP_PGA_GAIN_8_OR_MINUS_7 | InvertingFlag };
		static inline constexpr GainOption InvertingGain15 	{ LL_OPAMP_PGA_GAIN_16_OR_MINUS_15 | InvertingFlag };
		static inline constexpr GainOption InvertingGain31 	{ LL_OPAMP_PGA_GAIN_32_OR_MINUS_31 | InvertingFlag };
		static inline constexpr GainOption InvertingGain63 	{ LL_OPAMP_PGA_GAIN_64_OR_MINUS_63 | InvertingFlag };

		// Filter modes: PGGAIN bits 4 and 3 select an LL functional mode in which the inverting
		// input is used as a filter pin (VMSEL is forced to not connected). The magnitude bits are
		// left at zero, so these use the LL default gain of each mode.
		static inline constexpr GainOption FilterIO0 		{ OPAMP_CSR_PGGAIN_4 };
		static inline constexpr GainOption FilterIO0Bias 	{ OPAMP_CSR_PGGAIN_3 };
		static inline constexpr GainOption FilterIO0Io1Bias 	{ OPAMP_CSR_PGGAIN_4 | OPAMP_CSR_PGGAIN_3 };
	};


private:
	// HAL_OPAMP_SelfCalibrate waits 2 ms per trim step (the datasheet offset trim settling time).
	static inline constexpr uint32 OffsetTrimDelayMs 	{ 2 };
	// Middle of the 5-bit trim field; the binary search then moves in steps of 8, 4, 2, 1.
	static inline constexpr uint32 TrimMiddle 		{ 16 };
	static inline constexpr uint32 TrimMax 			{ 31 };
	// PGGAIN bits that select the magnitude inside the PGA, and the bits that select a filter mode.
	static inline constexpr uint32 PgaGainBits 		{ OPAMP_CSR_PGGAIN_2 | OPAMP_CSR_PGGAIN_1 | OPAMP_CSR_PGGAIN_0 };
	static inline constexpr uint32 PgaFilterBits 		{ OPAMP_CSR_PGGAIN_4 | OPAMP_CSR_PGGAIN_3 };


public:
	OPAMPAdapterG4() = default;
	OPAMPAdapterG4(OPAMP_TypeDef *opamp): OPAMPAdapter(opamp) { }


	virtual ResultStatus Enable() override {
		ResultStatus status = CheckAccess();
		if (status != ResultStatus::ok) {
			return status;
		}

		if (LL_OPAMP_IsEnabled(opampHandle) != 0) {
			return ResultStatus::ok;
		}

		LL_OPAMP_Enable(opampHandle);
		// The output is not valid until the datasheet startup time has passed.
		System::DelayUs(LL_OPAMP_DELAY_STARTUP_US);
		return ResultStatus::ok;
	}


	virtual ResultStatus Disable() override {
		ResultStatus status = CheckAccess();
		if (status != ResultStatus::ok) {
			return status;
		}

		if (LL_OPAMP_IsEnabled(opampHandle) != 0) {
			LL_OPAMP_Disable(opampHandle);
		}
		return ResultStatus::ok;
	}


	virtual ResultStatus SetGain(GainOption gain) override {
		if (parameters.mode != Mode::ProgrammableGain) {
			return ResultStatus::notReady;
		}

		ResultStatus status = CheckAccess();
		if (status != ResultStatus::ok) {
			return status;
		}

		// Only the magnitude bits can change while running. A different sign or filter mode
		// changes VMSEL or PGGAIN[4:3], and those are applied by SetParameters() with the amplifier off.
		if (IsInverting(gain) != IsInverting(parameters.gain)) {
			return ResultStatus::invalidParameter;
		}

		if ((gain.Get() & PgaFilterBits) != (parameters.gain.Get() & PgaFilterBits)) {
			return ResultStatus::invalidParameter;
		}

		LL_OPAMP_SetPGAGain(opampHandle, gain.Get() & PgaGainBits);
		parameters.gain = gain;
		return ResultStatus::ok;
	}


	// Offset calibration per RM0440 (CALON, CALSEL, USERTRIM, OUTCAL). The amplifier is off before and after,
	// and the trim values stay in CSR until the next reset.
	virtual ResultStatus Calibrate() override {
		ResultStatus status = CheckAccess();
		if (status != ResultStatus::ok) {
			return status;
		}

		// OPAMPINTEN must be clear during calibration, so it is saved here and restored at the end.
		const uint32 internalOutput = LL_OPAMP_GetInternalOutput(opampHandle);
		LL_OPAMP_Disable(opampHandle);
		LL_OPAMP_SetInternalOutput(opampHandle, LL_OPAMP_INTERNAL_OUTPUT_DISABLED);

		// FORCEVP ties the non-inverting input to the internal reference. USERTRIM makes the chip
		// apply the trim values written below instead of the factory trim.
		opampHandle->CSR |= OPAMP_CSR_FORCEVP;
		LL_OPAMP_SetTrimmingMode(opampHandle, LL_OPAMP_TRIMMING_USER);
		LL_OPAMP_SetMode(opampHandle, LL_OPAMP_MODE_CALIBRATION);
		LL_OPAMP_Enable(opampHandle);

		// NMOS pair against 90% VDDA first, then PMOS pair against 10% VDDA. Same order as HAL.
		FindTrimming(LL_OPAMP_TRIMMING_NMOS);
		FindTrimming(LL_OPAMP_TRIMMING_PMOS);

		LL_OPAMP_Disable(opampHandle);
		LL_OPAMP_SetMode(opampHandle, LL_OPAMP_MODE_FUNCTIONAL);
		opampHandle->CSR &= ~OPAMP_CSR_FORCEVP;
		LL_OPAMP_SetInternalOutput(opampHandle, internalOutput);
		return ResultStatus::ok;
	}


protected:
	virtual ResultStatus Initialization() override {
		ResultStatus status = BeforeInitialization();
		if (status != ResultStatus::ok) {
			return status;
		}

		status = CheckAccess();
		if (status != ResultStatus::ok) {
			return status;
		}

		status = CheckParameters();
		if (status != ResultStatus::ok) {
			return status;
		}

		// VMSEL and VPSEL take effect only with the amplifier off. Enable() starts it again.
		LL_OPAMP_Disable(opampHandle);
		LL_OPAMP_SetInputNonInverting(opampHandle, parameters.inputPlus.Get());
		LL_OPAMP_SetFunctionalMode(opampHandle, CastFunctionalMode());
		LL_OPAMP_SetPGAGain(opampHandle, GetGainField() & PgaGainBits);

		if (parameters.internalOutput) {
			LL_OPAMP_SetInternalOutput(opampHandle, LL_OPAMP_INTERNAL_OUTPUT_ENABLED);
		} else {
			LL_OPAMP_SetInternalOutput(opampHandle, LL_OPAMP_INTERNAL_OUTPUT_DISABLED);
		}

		if (parameters.highSpeed) {
			LL_OPAMP_SetPowerMode(opampHandle, LL_OPAMP_POWERMODE_HIGHSPEED);
		} else {
			LL_OPAMP_SetPowerMode(opampHandle, LL_OPAMP_POWERMODE_NORMALSPEED);
		}

		return AfterInitialization();
	}


private:
	// Every write goes through here. A clock-gated register must not be touched, and a locked CSR ignores writes.
	ResultStatus CheckAccess() const {
		if (LL_APB2_GRP1_IsEnabledClock(LL_APB2_GRP1_PERIPH_SYSCFG) == 0) {
			return ResultStatus::notReady;
		}

		if (LL_OPAMP_IsLocked(opampHandle) != 0) {
			return ResultStatus::accessError;
		}

		return ResultStatus::ok;
	}


	// Rejects a configuration that needs an inverting input pin but was given no pin.
	ResultStatus CheckParameters() const {
		bool standaloneNeedsInput = (parameters.mode == Mode::Standalone);
		bool inverting = (parameters.mode == Mode::ProgrammableGain) && IsInverting(parameters.gain);

		if ((standaloneNeedsInput || inverting) && parameters.inputMinus == InputMinus::NotConnected) {
			return ResultStatus::invalidParameter;
		}

		return ResultStatus::ok;
	}


	static constexpr bool IsInverting(GainOption gain) {
		return (gain.Get() & Gain::InvertingFlag) != 0;
	}


	// PGGAIN field of the current gain, or 0 outside the PGA mode. The gain bits are used only in that mode.
	uint32 GetGainField() const {
		if (parameters.mode != Mode::ProgrammableGain) {
			return 0;
		}

		return parameters.gain.Get() & OPAMP_CSR_PGGAIN;
	}


	// Builds the VMSEL and PGGAIN[4:3] value for SetFunctionalMode. LL clears those bits first, so the
	// PGA magnitude bits are written separately.
	uint32 CastFunctionalMode() const {
		switch (parameters.mode) {
			case Mode::Standalone: return parameters.inputMinus.Get();
			case Mode::Follower: return LL_OPAMP_MODE_FOLLOWER;
			case Mode::ProgrammableGain: {
				uint32 inputMinusSelection = LL_OPAMP_INPUT_INVERT_CONNECT_NO;
				if (IsInverting(parameters.gain)) {
					inputMinusSelection = parameters.inputMinus.Get();
				}
				return inputMinusSelection | (GetGainField() & PgaFilterBits);
			}
		}

		SystemAbort();
		return 0;
	}


	// One binary search over a trim field, with the calibration reference selected by the pair.
	void FindTrimming(uint32 transistorsDiffPair) {
		LL_OPAMP_SetCalibrationSelection(opampHandle, transistorsDiffPair);

		uint32 trimming = TrimMiddle;
		uint32 delta = TrimMiddle / 2;
		while (delta != 0) {
			LL_OPAMP_SetTrimmingValue(opampHandle, transistorsDiffPair, trimming);
			System::DelayMs(OffsetTrimDelayMs);

			if (LL_OPAMP_IsCalibrationOutputSet(opampHandle) != 0) {
				trimming += delta;
			} else {
				trimming -= delta;
			}
			delta >>= 1;
		}

		// The search can stop one step short. If OUTCAL is still set at the final value, one more step
		// is needed. The step is capped at the field maximum so it cannot carry into the other trim field.
		LL_OPAMP_SetTrimmingValue(opampHandle, transistorsDiffPair, trimming);
		System::DelayMs(OffsetTrimDelayMs);

		if ((LL_OPAMP_IsCalibrationOutputSet(opampHandle) != 0) && (trimming < TrimMax)) {
			trimming++;
			LL_OPAMP_SetTrimmingValue(opampHandle, transistorsDiffPair, trimming);
		}
	}
};
