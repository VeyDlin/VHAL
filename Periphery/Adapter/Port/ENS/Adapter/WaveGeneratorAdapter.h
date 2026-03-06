#pragma once
#include <Adapter/IAdapter.h>

#define VHAL_WAVEGENERATOR_ADAPTER


template<typename HandleType>
class WaveGeneratorAdapter : public IAdapter {
public:
	struct ConfigOption : IOptionFlag<uint8> { 
		using IOptionFlag::IOptionFlag; 
	};
	
	struct CurrentSelectOption : IOption<uint8> { 
		using IOption::IOption; 
	};

	struct Parameters {
		ConfigOption config;
		uint8 restTime = 0;              // <7:0>  rest time (us)
		uint32 silentTime = 0;           // <31:0> silent time (us)
		uint32 halfWavePeriod = 0;       // <31:0> positive half-wave period (us)
		uint32 negHalfWavePeriod = 0;    // <31:0> negative half-wave period (us)
		uint8 clockFreqDiv = 0;          // <7:0>  clock frequency (MHz)
		uint16 alternationLimit = 0;     // <15:0> alternation period (clocks)
		uint16 alternationSilentLimit = 0; // <15:0> silent duration per alternation (clocks)
		uint16 delayLimit = 0;           // <15:0> initial delay (clocks)
		uint8 negScale = 0;              // <7:0>  negative side scale (unsigned)
		uint8 negOffset = 0;             // <7:0>  negative side offset (unsigned)
		CurrentSelectOption currentSelect; // <2:0>
		uint8 switchConfig = 0;          // <7:0>  electrode switch bitmask
	};


protected:
	HandleType *waveGenHandle = nullptr;
	Parameters parameters;
	uint32 intRegShadow = 0; // Shadow for INT_REG (asymmetric read/write register)


	struct InterruptEvent {
		bool firstAddress;       // First address interrupt triggered
		bool secondAddress;      // Second address interrupt triggered
		uint8 readFirstAddr;     // Current first address value (from read-back)
		uint8 readSecondAddr;    // Current second address value (from read-back)
	};


public:
	std::function<void(InterruptEvent)> onInterrupt;

	WaveGeneratorAdapter() = default;
	WaveGeneratorAdapter(HandleType *waveGen) : waveGenHandle(waveGen) {}
	virtual ~WaveGeneratorAdapter() = default;

	virtual void SetParameters(Parameters val) = 0;
	virtual void Start() = 0;
	virtual void Stop() = 0;
	virtual void WriteWaveformPoint(uint8 address, uint8 value) = 0;
	virtual void WriteWaveformPointDriverB(uint8 address, uint8 value, uint8 channel) = 0;
	virtual void IrqHandler() = 0;
	virtual void SetInterruptAddresses(uint8 firstAddress, uint8 secondAddress) = 0;
	virtual void EnableInterrupt(bool enable) = 0;
};
