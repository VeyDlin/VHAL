#pragma once
#include "IAdapter.h"
#include "GPIOAdapter.h"

#define AUSED_SPI_ADAPTER



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
	SPI_TypeDef *spiHandle;


	uint32 inputBusClockHz = 0;
	uint32 baudRatePrescaler;


	uint32 timeout = 1000;
	bool enableContinuous = false;
	bool isTwoLineInterrupt = false;


	Parameters parameters;

	bool continuousAsyncRxMode = false;
	Status::statusType rxState = Status::ready;
	uint16 rxDataNeed = 0;
	uint16 rxDataCounter = 0;
	uint8 *rxDataPointer = nullptr;
	uint8 lastRxData = 0;

	bool continuousAsyncTxMode = false;
	Status::statusType txState = Status::ready;
	uint16 txDataNeed = 0;
	uint16 txDataCounter = 0;
	uint8 *txDataPointer = nullptr;


public:
	std::function<void(Irq irqType)> onInterrupt;
	std::function<void(Error errorType)> onError;



public:
	SPIAdapter() { }
	SPIAdapter(SPI_TypeDef *spi, uint32 busClockHz = 0): spiHandle(spi), inputBusClockHz(busClockHz) { }




	template <typename DataType>
	inline Status::statusType Write(DataType data) {
		ChipSelect(true);
		auto status = WriteByteArray(reinterpret_cast<uint8*>(&data), sizeof(DataType));
		ChipSelect(false);
		return status;
	}





	template <typename DataType>
	inline Status::statusType WriteArray(DataType* buffer, uint32 size) {
		ChipSelect(true);
		auto status = WriteByteArray(reinterpret_cast<uint8*>(buffer), sizeof(DataType) * size);
		ChipSelect(false);
		return status;
	}





	template <typename DataType>
	inline Status::statusType WriteArray(const DataType* buffer, uint32 size) {
		return WriteArray<DataType>(const_cast<DataType*>(buffer), size);
	}





	template <typename DataType>
	inline Status::info<DataType> Read(uint32 size = 1) {
		auto output = Status::info<DataType>();
		ChipSelect(true);
		output.type = ReadByteArray(reinterpret_cast<uint8*>(&output.data), sizeof(DataType) * size);
		ChipSelect(false);
		return output;
	}





	template <typename DataType>
	inline Status::statusType ReadArray(DataType* buffer, uint32 size) {
		auto output = Status::info<DataType>();
		ChipSelect(true);
		output.type = ReadByteArray(reinterpret_cast<uint8*>(buffer), sizeof(DataType) * size);
		ChipSelect(false);
		return output;
	}





	template <typename DataType>
	inline Status::info<DataType> WriteRead(DataType data) {
		auto output = Status::info<DataType>();
		ChipSelect(true);
		output.type = WriteReadByteArray(reinterpret_cast<uint8*>(&data), reinterpret_cast<uint8*>(&output.data), sizeof(DataType));
		ChipSelect(false);
		return output;
	}





	template <typename DataType>
	inline Status::info<DataType> WriteReadArray(DataType *txBuffer, DataType *rxBuffer, uint32 size) {
		ChipSelect(true);
		auto status = WriteReadByteArray(reinterpret_cast<uint8*>(txBuffer), reinterpret_cast<uint8*>(rxBuffer), sizeof(DataType) * size);
		ChipSelect(false);
		return status;
	}





	// ---------------





	template <typename DataType>
	inline Status::statusType WriteAsync(DataType &data) {
		ChipSelect(true);
		return WriteByteArrayAsync(reinterpret_cast<uint8*>(&data), sizeof(DataType));
	}





	template <typename DataType>
	inline Status::statusType WriteArrayAsync(DataType* buffer, uint32 size) {
		ChipSelect(true);
		return WriteByteArrayAsync(reinterpret_cast<uint8*>(buffer), sizeof(DataType) * size);
	}





	template <typename DataType>
	inline Status::statusType WriteArrayAsync(const DataType* buffer, uint32 size) {
		return WriteArray<DataType>(const_cast<DataType*>(buffer), size);
	}





	template <typename DataType>
	inline Status::statusType ReadAsync(DataType* buffer) {
		ChipSelect(true);
		return ReadByteArrayAsync(reinterpret_cast<uint8*>(buffer), sizeof(DataType));
	}





	template <typename DataType>
	inline Status::statusType ReadArrayAsync(DataType* buffer, uint32 size) {
		ChipSelect(true);
		return ReadByteArrayAsync(reinterpret_cast<uint8*>(buffer), sizeof(DataType) * size);
	}





	template <typename DataType>
	inline Status::info<DataType> WriteReadAsync(DataType *txBuffer, DataType *rxBuffer) {
		ChipSelect(true);
		return WriteReadByteArrayAsync(reinterpret_cast<uint8*>(txBuffer), reinterpret_cast<uint8*>(rxBuffer), sizeof(DataType));
	}





	template <typename DataType>
	inline Status::info<DataType> WriteReadArrayAsync(DataType *txBuffer, DataType *rxBuffer, uint32 size) {
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
	virtual Status::info<uint32> SetParameters(Parameters val) {
		auto status = Status::info<uint32>();
		parameters = val;

		status.data = CalculatePrescaler();

		status.type = Initialization();
		if(status.IsError()) {
			return status;
		}


		if(parameters.chipSelectPin == nullptr) {
			return status;
		}

		auto mode = parameters.mode == Mode::Slave ?
				GPIOAdapter::Mode::InterruptFalling :
				GPIOAdapter::Mode::Output;

		status.type = parameters.chipSelectPin->SetParameters({
			.mode = mode,
			.pull = GPIOAdapter::Pull::None,
			.speed = GPIOAdapter::Speed::Medium
		});
		parameters.chipSelectPin->SetInversion(parameters.chipSelect == ChipSelect::Low);


		ChipSelect(false);

		return status;
	}


	virtual void SetTimeout(uint32 val) {
		timeout = val;
	}


	virtual Status::statusType SetContinuousAsyncTxMode(bool mode) {
		auto status = mode ? StartContinuousAsyncTxMode() : StopContinuousAsyncTxMode();
		if(status == Status::ok) {
			continuousAsyncTxMode = mode;
		}
		return status;
	}


	virtual Status::statusType SetContinuousAsyncRxMode(bool mode) {
		auto status = mode ? StartContinuousAsyncRxMode() : StopContinuousAsyncRxMode();
		if(status == Status::ok) {
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
	virtual Status::statusType Initialization() = 0;
	virtual uint32 CalculatePrescaler() = 0;

	virtual Status::statusType WriteByteArray(uint8 *buffer, uint32 size) = 0;
	virtual Status::statusType ReadByteArray(uint8 *buffer, uint32 size) = 0;
	virtual Status::statusType WriteReadByteArray(uint8 *txBuffer, uint8 *rxBuffer, uint32 size) = 0;

	virtual Status::statusType WriteByteArrayAsync(uint8 *buffer, uint32 size) = 0;
	virtual Status::statusType ReadByteArrayAsync(uint8 *buffer, uint32 size) = 0;
	virtual Status::statusType WriteReadByteArrayAsync(uint8 *txBuffer, uint8 *rxBuffer, uint32 size) = 0;


	virtual Status::statusType StartContinuousAsyncRxMode() {
		return Status::notSupported;
	}

	virtual Status::statusType StopContinuousAsyncRxMode() {
		return Status::notSupported;
	}

	virtual Status::statusType StartContinuousAsyncTxMode() {
		return Status::notSupported;
	}

	virtual Status::statusType StopContinuousAsyncTxMode() {
		return Status::notSupported;
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















