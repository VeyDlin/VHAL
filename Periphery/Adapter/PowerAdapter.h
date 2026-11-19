#pragma once
#include "IAdapter.h"

#define VHAL_POWER_ADAPTER


// Power management of the chip: low-power modes, wakeup pins, the reset and wakeup causes, and the supply voltage monitor
// (STM32 PWR with the reset flags of RCC, ESP32 sleep modes, nRF POWER/RESET).
//
// The low-power modes differ in detail between chips, so a mode is a port option (ModeOption); every port names its
// modes in the same order of depth:
//   - sleep: the core stops, peripherals and RAM keep running, any interrupt wakes it;
//   - stop (deep sleep): clocks stop, RAM and registers are kept, wakeup by the wakeup pins, the RTC or the interrupts the
//     port documents; execution goes on after EnterLowPower() and the BSP restarts the clocks it needs;
//   - standby / shutdown: almost everything is off and RAM is lost; wakeup is a reset, and GetWakeupCause() tells why.
//
// The voltage monitor interrupt arrives through IrqHandler(); the application calls it from the monitor's vector (PVD on
// STM32).
template<typename HandleType>
class PowerAdapter : public IAdapter {
public:
	enum class ResetCause : uint8 {
		PowerOn,
		BrownOut,
		Pin,
		Software,
		IndependentWatchdog,
		WindowWatchdog,
		// Woke from a mode that loses RAM (standby or shutdown).
		LowPowerWakeup,
		// A reset caused by entering a low-power mode while the option bytes forbid it.
		LowPowerFault,
		OptionLoad,
		Unknown
	};

	enum class Edge : uint8 {
		Rising,
		Falling
	};

	// A low-power mode of the port, from the port's named constants.
	struct ModeOption : IOption<uint8> {
		using IOption::IOption;
	};

	// A wakeup pin of the port (not a GPIO number: the chip's dedicated wakeup inputs).
	struct WakeupPinOption : IOption<uint8> {
		using IOption::IOption;
	};

	// A voltage monitor threshold of the port.
	struct ThresholdOption : IOption<uint8> {
		using IOption::IOption;
	};

	struct Parameters {
	};

	// Interrupt context. low is true when the supply fell below the threshold, false when it rose above it again.
	std::function<void(bool low)> onVoltageChange;


protected:
	HandleType *powerHandle = nullptr;
	Parameters parameters;


public:
	PowerAdapter() = default;


	PowerAdapter(HandleType *power) : powerHandle(power) {
	}


	virtual ResultStatus SetParameters(Parameters val) {
		parameters = val;
		return Initialization();
	}


	// Enters the mode and returns after the wakeup for the modes that keep RAM. Modes that lose RAM do not return: the
	// chip resets on wakeup. Thread context. invalidParameter for a mode the port does not have.
	virtual ResultStatus EnterLowPower(ModeOption mode) = 0;


	// Lets the pin wake the chip from the low-power modes on the given edge.
	virtual ResultStatus EnableWakeupPin(WakeupPinOption pin, Edge edge) = 0;


	virtual ResultStatus DisableWakeupPin(WakeupPinOption pin) = 0;


	// Why the chip last reset. The cause is read once, before ClearResetCause(); the port reports the most specific
	// cause when the hardware sets several flags (a watchdog reset also sets the pin flag on STM32).
	virtual ResetCause GetResetCause() const = 0;


	// Clears the reset flags so the next reset reports only its own cause.
	virtual void ClearResetCause() = 0;


	// The wakeup pin that woke the chip from a mode that loses RAM, or notAvailable when the last start was not such a
	// wakeup or the wakeup came from another source (the RTC).
	virtual Result<WakeupPinOption> GetWakeupPin() const = 0;


	// Starts the supply voltage monitor; onVoltageChange fires on each crossing of the threshold.
	virtual ResultStatus EnableVoltageMonitor(ThresholdOption threshold) = 0;


	virtual ResultStatus DisableVoltageMonitor() = 0;


	// True while the supply is below the monitor threshold. False when the monitor is off.
	virtual bool IsVoltageLow() const = 0;


	virtual void IrqHandler() = 0;


protected:
	// Calls BeforeInitialization() and AfterInitialization().
	virtual ResultStatus Initialization() = 0;


	virtual inline void CallVoltageChange(bool low) {
		if (onVoltageChange != nullptr) {
			onVoltageChange(low);
		}
	}
};
