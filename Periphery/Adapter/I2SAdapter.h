#pragma once
#include "IAdapter.h"

#define VHAL_I2S_ADAPTER


template<typename HandleType>
class I2SAdapter : public IAdapter {
public:
	enum class Mode { Master, Slave };
	enum class Direction { Tx, Rx, TxRx };
	enum class Standard { Philips, MSB, LSB, PCMShort, PCMLong };
	enum class DataFormat { B16, B16Extended, B24, B32 };
	enum class ClockPolarity { Low, High };

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


	virtual Status::statusType SetParameters(Parameters val) {
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
	virtual Status::statusType Transmit(uint16 *buffer, uint32 size) = 0;
	virtual Status::statusType Receive(uint16 *buffer, uint32 size) = 0;
	virtual Status::statusType TransmitReceive(uint16 *txBuffer, uint16 *rxBuffer, uint32 size) = 0;

	// Async (DMA/interrupt)
	virtual Status::statusType TransmitAsync(uint16 *buffer, uint32 size) = 0;
	virtual Status::statusType ReceiveAsync(uint16 *buffer, uint32 size) = 0;
	virtual Status::statusType TransmitReceiveAsync(uint16 *txBuffer, uint16 *rxBuffer, uint32 size) = 0;

	// Circular (continuous streaming)
	virtual Status::statusType TransmitCircular(uint16 *buffer, uint32 size) = 0;
	virtual Status::statusType ReceiveCircular(uint16 *buffer, uint32 size) = 0;

	// Control
	virtual Status::statusType StopTransmit() = 0;
	virtual Status::statusType StopReceive() = 0;

	virtual void IrqHandler() = 0;


protected:
	virtual Status::statusType Initialization() = 0;


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
