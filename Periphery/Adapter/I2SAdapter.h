#pragma once
#include "IAdapter.h"

#define VHAL_I2S_ADAPTER


template<typename HandleType>
class I2SAdapter : public IAdapter {
public:
	enum class Mode { Master, Slave };
	enum class Direction { Tx, Rx, TxRx };
	enum class Standard { Philips, MSB, LSB, PCMShort, PCMLong, PDM };
	enum class DataFormat { B16, B16Extended, B24, B32 };
	enum class ClockPolarity { Low, High };
	// PDM bits per output sample (the decimation ratio). The PDM clock is sampleRateHz times this ratio, so it also sets the
	// microphone's clock: at 16 kHz, X64 gives 1.024 MHz and X128 2.048 MHz. Standard::PDM only.
	enum class PdmDecimation { X64, X128 };

	enum class Irq { TxComplete, TxHalfComplete, RxComplete, RxHalfComplete };
	enum class Error { None, Overrun, Underrun, FrameError };

	struct ClockSourceOption : IOption<uint32> {
		using IOption::IOption;
	};

	struct SlotCountOption : IOption<uint8> {
		using IOption::IOption;
	};

	struct SlotOption : IOptionFlag<uint32> {
		using IOptionFlag::IOptionFlag;
		// operator| returns the base type; without this a combination such as S0 | S1 cannot initialize a SlotOption.
		constexpr SlotOption(const IOptionFlag<uint32>& flags) noexcept : IOptionFlag(flags) { }
	};

	struct Parameters {
		Mode mode = Mode::Master;
		Direction direction = Direction::Tx;
		Standard standard = Standard::Philips;
		DataFormat dataFormat = DataFormat::B16;
		ClockPolarity clockPolarity = ClockPolarity::Low;
		ClockSourceOption clockSource;
		uint32 sampleRateHz = 44100;
		bool mclkOutput = true;
		SlotCountOption slotCount;
		SlotOption activeSlots;
		PdmDecimation pdmDecimation = PdmDecimation::X64;
	};


protected:
	HandleType *i2sHandle;
	Parameters parameters;
	uint32 inputBusClockHz = 0;
	uint32 timeout = 1000;


public:
	std::function<void(Irq irqType)> onInterrupt;
	std::function<void(Error errorType)> onError;


public:
	I2SAdapter() = default;
	I2SAdapter(HandleType *i2s, uint32 busClockHz = 0) : i2sHandle(i2s), inputBusClockHz(busClockHz) { }


	// Replaces onInterrupt while its interrupt may run. See "Changing a callback while its interrupt may run" in the wiki.
	void SetOnInterrupt(std::function<void(Irq irqType)> handler) {
		std::function<void(Irq irqType)> old;
		System::CriticalSection(true);
		old = std::move(onInterrupt);
		onInterrupt = std::move(handler);
		System::CriticalSection(false);
	}


	// Replaces onError while its interrupt may run. See "Changing a callback while its interrupt may run" in the wiki.
	void SetOnError(std::function<void(Error errorType)> handler) {
		std::function<void(Error errorType)> old;
		System::CriticalSection(true);
		old = std::move(onError);
		onError = std::move(handler);
		System::CriticalSection(false);
	}


	virtual ResultStatus SetParameters(Parameters val) {
		parameters = val;
		return Initialization();
	}


	const Parameters& GetParameters() const {
		return parameters;
	}


	virtual void SetTimeout(uint32 val) {
		timeout = val;
	}


	// Sync
	virtual ResultStatus Transmit(uint16 *buffer, uint32 size) = 0;
	virtual ResultStatus Receive(uint16 *buffer, uint32 size) = 0;
	virtual ResultStatus TransmitReceive(uint16 *txBuffer, uint16 *rxBuffer, uint32 size) = 0;

	// Async (DMA/interrupt)
	virtual ResultStatus TransmitAsync(uint16 *buffer, uint32 size) = 0;
	virtual ResultStatus ReceiveAsync(uint16 *buffer, uint32 size) = 0;
	virtual ResultStatus TransmitReceiveAsync(uint16 *txBuffer, uint16 *rxBuffer, uint32 size) = 0;

	// Circular (continuous streaming)
	virtual ResultStatus TransmitCircular(uint16 *buffer, uint32 size) = 0;
	virtual ResultStatus ReceiveCircular(uint16 *buffer, uint32 size) = 0;

	// Control
	virtual ResultStatus StopTransmit() = 0;
	virtual ResultStatus StopReceive() = 0;

	virtual void IrqHandler() = 0;


protected:
	virtual ResultStatus Initialization() = 0;


	virtual inline void CallInterrupt(Irq irqType) {
		if (onInterrupt != nullptr) {
			onInterrupt(irqType);
		}
	}

	virtual inline void CallError(Error error) {
		if (onError != nullptr) {
			onError(error);
		}
	}
};
