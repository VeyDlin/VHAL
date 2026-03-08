#pragma once
#include "IAdapter.h"
#include "GPIOAdapter.h"

#define VHAL_SPI_ADAPTER


template<typename HandleType>
class SPIAdapter: public IAdapter {
public:

	enum class Mode { Master, Slave };
	enum class Direction { Tx, Rx, TxRx };
	enum class ClockPolarity { Low, High };
	enum class ClockPhase { Edge1, Edge2 };
	enum class FirstBit { MSB, LSB };
	enum class ChipSelect { Low, High };

	enum class Irq { Tx, Rx, TxRx };

	enum class Error { None, Overrun, ModeFault, FrameFormat };


	struct Parameters {
		Mode mode = Mode::Master;
		Direction direction = Direction::TxRx;
		ClockPolarity clockPolarity = ClockPolarity::Low;
		ClockPhase clockPhase = ClockPhase::Edge1;
		FirstBit firstBit = FirstBit::MSB;
		uint32 maxSpeedHz = 100;
		GPIOAdapter *chipSelectPin = nullptr;
		ChipSelect chipSelect = ChipSelect::Low;
	};





protected:
	HandleType *spiHandle;


	uint32 inputBusClockHz = 0;
	uint32 baudRatePrescaler;


	uint32 timeout = 1000;
	bool enableContinuous = false;
	bool isTwoLineInterrupt = false;


	Parameters parameters;

	bool continuousAsyncRxMode = false;
	ResultStatus rxState = ResultStatus::ready;
	uint16 rxDataNeed = 0;
	uint16 rxDataCounter = 0;
	uint8 *rxDataPointer = nullptr;
	uint8 lastRxData = 0;

	bool continuousAsyncTxMode = false;
	ResultStatus txState = ResultStatus::ready;
	uint16 txDataNeed = 0;
	uint16 txDataCounter = 0;
	uint8 *txDataPointer = nullptr;


public:
	std::function<void(Irq irqType)> onInterrupt;
	std::function<void(Error errorType)> onError;



public:
	SPIAdapter() = default;
	SPIAdapter(HandleType *spi, uint32 busClockHz = 0): spiHandle(spi), inputBusClockHz(busClockHz) { }




	template <typename DataType>
	inline ResultStatus Write(DataType data) {
		ChipSelect(true);
		auto status = WriteByteArray(reinterpret_cast<uint8*>(&data), sizeof(DataType));
		ChipSelect(false);
		return status;
	}





	template <typename DataType>
	inline ResultStatus WriteArray(DataType* buffer, uint32 size) {
		ChipSelect(true);
		auto status = WriteByteArray(reinterpret_cast<uint8*>(buffer), sizeof(DataType) * size);
		ChipSelect(false);
		return status;
	}





	template <typename DataType>
	inline ResultStatus WriteArray(const DataType* buffer, uint32 size) {
		return WriteArray<DataType>(const_cast<DataType*>(buffer), size);
	}





	template <typename DataType>
	inline Result<DataType> Read(uint32 size = 1) {
		DataType data;
		ChipSelect(true);
		auto status = ReadByteArray(reinterpret_cast<uint8*>(&data), sizeof(DataType) * size);
		ChipSelect(false);
		return Result<DataType>::Capture(status, data);
	}





	template <typename DataType>
	inline ResultStatus ReadArray(DataType* buffer, uint32 size) {
		ChipSelect(true);
		auto status = ReadByteArray(reinterpret_cast<uint8*>(buffer), sizeof(DataType) * size);
		ChipSelect(false);
		return status;
	}





	template <typename DataType>
	inline Result<DataType> WriteRead(DataType data) {
		DataType outData;
		ChipSelect(true);
		auto status = WriteReadByteArray(reinterpret_cast<uint8*>(&data), reinterpret_cast<uint8*>(&outData), sizeof(DataType));
		ChipSelect(false);
		return Result<DataType>::Capture(status, outData);
	}





	template <typename DataType>
	inline ResultStatus WriteReadArray(DataType *txBuffer, DataType *rxBuffer, uint32 size) {
		ChipSelect(true);
		auto status = WriteReadByteArray(reinterpret_cast<uint8*>(txBuffer), reinterpret_cast<uint8*>(rxBuffer), sizeof(DataType) * size);
		ChipSelect(false);
		return status;
	}





	// ---------------





	template <typename DataType>
	inline ResultStatus WriteAsync(DataType &data) {
		ChipSelect(true);
		return WriteByteArrayAsync(reinterpret_cast<uint8*>(&data), sizeof(DataType));
	}





	template <typename DataType>
	inline ResultStatus WriteArrayAsync(DataType* buffer, uint32 size) {
		ChipSelect(true);
		return WriteByteArrayAsync(reinterpret_cast<uint8*>(buffer), sizeof(DataType) * size);
	}





	template <typename DataType>
	inline ResultStatus WriteArrayAsync(const DataType* buffer, uint32 size) {
		return WriteArray<DataType>(const_cast<DataType*>(buffer), size);
	}





	template <typename DataType>
	inline ResultStatus ReadAsync(DataType* buffer) {
		ChipSelect(true);
		return ReadByteArrayAsync(reinterpret_cast<uint8*>(buffer), sizeof(DataType));
	}





	template <typename DataType>
	inline ResultStatus ReadArrayAsync(DataType* buffer, uint32 size) {
		ChipSelect(true);
		return ReadByteArrayAsync(reinterpret_cast<uint8*>(buffer), sizeof(DataType) * size);
	}





	template <typename DataType>
	inline ResultStatus WriteReadAsync(DataType *txBuffer, DataType *rxBuffer) {
		ChipSelect(true);
		return WriteReadByteArrayAsync(reinterpret_cast<uint8*>(txBuffer), reinterpret_cast<uint8*>(rxBuffer), sizeof(DataType));
	}





	template <typename DataType>
	inline ResultStatus WriteReadArrayAsync(DataType *txBuffer, DataType *rxBuffer, uint32 size) {
		ChipSelect(true);
		return WriteReadByteArrayAsync(reinterpret_cast<uint8*>(txBuffer), reinterpret_cast<uint8*>(rxBuffer), sizeof(DataType) * size);
	}





	// ---------------






	virtual void Continuous(bool enable) {
		if(enable) {
			ChipSelect(true);
			enableContinuous = true;
		} else {
			enableContinuous = false;
			ChipSelect(false);
		}
	}




	virtual void ChipSelect(bool isSelect) {
		if(parameters.chipSelectPin == nullptr || enableContinuous || parameters.mode == Mode::Slave) {
			return;
		}

		parameters.chipSelectPin->SetState(isSelect);
	}







public:
	virtual Result<uint32> SetParameters(Parameters val) {
		parameters = val;

		auto prescaler = CalculatePrescaler();

		auto initStatus = Initialization();
		if(initStatus != ResultStatus::ok) {
			return initStatus;
		}


		if(parameters.chipSelectPin == nullptr) {
			return Result<uint32>::Ok(prescaler);
		}

		auto mode = parameters.mode == Mode::Slave ?
				GPIOAdapter::Mode::InterruptFalling :
				GPIOAdapter::Mode::Output;

		auto gpioStatus = parameters.chipSelectPin->SetParameters({
			.mode = mode,
			.pull = GPIOAdapter::Pull::None,
			.speed = GPIOAdapter::Speed::Medium
		});
		parameters.chipSelectPin->SetInversion(parameters.chipSelect == ChipSelect::Low);


		ChipSelect(false);

		return Result<uint32>::Capture(gpioStatus, prescaler);
	}


	virtual void SetTimeout(uint32 val) {
		timeout = val;
	}


	virtual ResultStatus SetContinuousAsyncTxMode(bool mode) {
		auto status = mode ? StartContinuousAsyncTxMode() : StopContinuousAsyncTxMode();
		if(status == ResultStatus::ok) {
			continuousAsyncTxMode = mode;
		}
		return status;
	}


	virtual ResultStatus SetContinuousAsyncRxMode(bool mode) {
		auto status = mode ? StartContinuousAsyncRxMode() : StopContinuousAsyncRxMode();
		if(status == ResultStatus::ok) {
			continuousAsyncRxMode = mode;
		}
		return status;
	}


	virtual uint16 GetLastRxData() {
		return lastRxData;
	}


	virtual void IrqHandler() = 0;


	virtual void AbortReceive() = 0;
	virtual void AbortTransmit() = 0;


	virtual void AbortAll() {
		AbortTransmit();
		AbortReceive();
	}





protected:
	virtual ResultStatus Initialization() = 0;
	virtual uint32 CalculatePrescaler() = 0;

	virtual ResultStatus WriteByteArray(uint8 *buffer, uint32 size) = 0;
	virtual ResultStatus ReadByteArray(uint8 *buffer, uint32 size) = 0;
	virtual ResultStatus WriteReadByteArray(uint8 *txBuffer, uint8 *rxBuffer, uint32 size) = 0;

	virtual ResultStatus WriteByteArrayAsync(uint8 *buffer, uint32 size) = 0;
	virtual ResultStatus ReadByteArrayAsync(uint8 *buffer, uint32 size) = 0;
	virtual ResultStatus WriteReadByteArrayAsync(uint8 *txBuffer, uint8 *rxBuffer, uint32 size) = 0;


	virtual ResultStatus StartContinuousAsyncRxMode() {
		return ResultStatus::notSupported;
	}

	virtual ResultStatus StopContinuousAsyncRxMode() {
		return ResultStatus::notSupported;
	}

	virtual ResultStatus StartContinuousAsyncTxMode() {
		return ResultStatus::notSupported;
	}

	virtual ResultStatus StopContinuousAsyncTxMode() {
		return ResultStatus::notSupported;
	}


	virtual inline void CallInterrupt(Irq irqType) {
		if(onInterrupt != nullptr) {
			onInterrupt(irqType);
		}
	 }

	virtual inline void CallError(Error error) {
		if(onError != nullptr) {
			onError(error);
		}
	 }
};















