#pragma once
#include "IAdapter.h"

#define VHAL_OPAMP_ADAPTER


// On-chip operational amplifier (STM32G4 OPAMP; the ENS programmable gain amplifier is the same idea).
// Inputs, gains and output routes differ per chip, so they are port options with named constants in each port.
template<typename HandleType>
class OPAMPAdapter : public IAdapter {
public:
	enum class Mode : uint8 {
		// Both inputs and the feedback network are external pins.
		Standalone,
		// Unity-gain buffer: the output is fed back to the inverting input inside the chip.
		Follower,
		// Gain set by the internal resistor network (GainOption); the inverting input is internal or a filter pin.
		ProgrammableGain
	};

	struct InputPlusOption : IOption<uint32> {
		using IOption::IOption;
	};

	struct InputMinusOption : IOption<uint32> {
		using IOption::IOption;
	};

	struct GainOption : IOption<uint32> {
		using IOption::IOption;
	};

	struct Parameters {
		Mode mode = Mode::Follower;
		InputPlusOption inputPlus;
		// Used in Standalone and in ProgrammableGain with an external filter; ignored in Follower.
		InputMinusOption inputMinus;
		// Used in ProgrammableGain only.
		GainOption gain;
		// Route the output to the ADC inside the chip instead of (or as well as, where the port can) the output pin.
		bool internalOutput = false;
		// Higher bandwidth and slew rate at a higher supply current, where the port has it.
		bool highSpeed = false;
	};


protected:
	HandleType *opampHandle = nullptr;
	Parameters parameters;


public:
	OPAMPAdapter() = default;


	OPAMPAdapter(HandleType *opamp) : opampHandle(opamp) {
	}


	// Applies the parameters with the amplifier disabled; call Enable() afterwards.
	virtual ResultStatus SetParameters(Parameters val) {
		parameters = val;
		return Initialization();
	}


	const Parameters& GetParameters() const {
		return parameters;
	}


	virtual ResultStatus Enable() = 0;


	virtual ResultStatus Disable() = 0;


	// Changes the gain while running (ProgrammableGain only, else notReady).
	virtual ResultStatus SetGain(GainOption gain) = 0;


	// Trims the input offset of both input pairs with the amplifier disabled, and keeps the result until the next reset.
	// notSupported when the port has no trimming. Takes a few milliseconds; thread context.
	virtual ResultStatus Calibrate() = 0;


protected:
	// Calls BeforeInitialization() and AfterInitialization().
	virtual ResultStatus Initialization() = 0;
};
