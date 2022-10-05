#pragma once
#include "IAdapter.h"

#define AUSED_I2C_ADAPTER



class I2CAdapter: public IAdapter {
public:
	enum class DutyCycle { D2, D16_9 };
	enum class AddressingMode { B7, B10 };
	enum class Mode { Master, Slave };
	enum class Speed:uint16 {
		Low = 10,
		Standard = 100,
		Fast = 400,
		FastPlus = 1000
	};

	enum class Error { None, SlaveAddressMatch, Busy, ArbitrationLost, OverUnderRun, AcknowledgeFailure, MisplacedStartStop };


	struct Parameters {
		Speed speed = Speed::Standard;
		Mode mode = Mode::Master;
		AddressingMode addressingMode = AddressingMode::B7;
		uint8 slaveAddress = 0;
		DutyCycle dutyCycle = DutyCycle::D2;
		bool analogFilter = false;
		uint8 digitalFilter = 0;
		bool clockStretching = true;
		uint8 riseTimeNs = 100;
		uint8 fallTimeNs = 10;
	};



protected:
	I2C_TypeDef *i2cHandle;
	Parameters parameters;

	uint32 inputBusClockHz = 0;

	uint32 timeout = 1000;

	Status::statusType state = Status::ready;

	uint16 deviceAddress = 0;

	uint16 registerAddress = 0; 
	uint8 registerAddressSize = 0;

	uint32 rxDataNeed = 0;
	uint32 rxDataCounter = 0;
	uint8 *rxDataPointer = nullptr;

	uint32 txDataNeed = 0;
	uint32 txDataCounter = 0;
	uint8 *txDataPointer = nullptr;





public:
	std::function<void(uint8 data)> onSlaveRead;
	std::function<uint8()> onSlaveWrite;
	std::function<void()> onSlaveEndTransfer;
	std::function<void(Error errorType)> onError;
	// TODO: Add CheckDevice and ScanAsync


public:
	I2CAdapter() { }

	I2CAdapter(I2C_TypeDef *i2c, uint32 busClockHz):i2cHandle(i2c), inputBusClockHz(busClockHz) { }


	virtual Status::statusType SetParameters(Parameters val) {
		parameters = val;
		return Initialization();
	}


	virtual void SetTimeout(uint32 time) {
		timeout = time;
	}


	virtual Status::statusType Await() {
		// TODO: add slave listen
		// TODO: add timeout
		while(state != Status::ready && state != Status::error);

		if (state == Status::ready) {
			return Status::ok;
		}
		return Status::error;
	}


	Status::statusType SetSlaveLiisten(bool mode) {
		auto status = mode ? StartSlaveListen() : StopSlaveListen();
		//if(status == Status::ok) {
		//	continuousAsyncRxMode = mode; // TODO: В юарт это есть
		//}
		return status;
	}



	virtual Status::statusType CheckDevice(uint8 deviceAddress, uint16 repeat = 1) = 0;
	virtual Status::statusType CheckDeviceAsync(uint8 deviceAddress, uint16 repeat = 1) = 0;

	virtual Status::info<uint8> Scan(uint8 *listBuffer, uint8 size) = 0;
	virtual Status::info<uint8> ScanAsync(uint8 *listBuffer, uint8 size) = 0;

	virtual void IrqEventHandler() = 0;
	virtual void IrqErrorHandler() = 0;




protected:
	virtual Status::statusType WriteByteArray(uint8 device, uint16 address, uint8 addressSize, uint8* writeData, uint32 dataSize) = 0;
	virtual Status::statusType ReadByteArray(uint8 device, uint16 address, uint8 addressSize, uint8* readData, uint32 dataSize) = 0;

	virtual Status::statusType WriteByteArrayAsync(uint8 device, uint16 address, uint8 addressSize, uint8* writeData, uint32 dataSize) = 0;
	virtual Status::statusType ReadByteArrayAsync(uint8 device, uint16 address, uint8 addressSize, uint8* readData, uint32 dataSize) = 0;



	virtual Status::statusType Initialization() = 0;


	virtual Status::statusType StartSlaveListen() {
		return Status::notSupported;
	}

	virtual Status::statusType StopSlaveListen() {
		return Status::notSupported;
	}


	virtual inline void CallSlaveRead(uint8 data) {
		if(onSlaveRead != nullptr) {
			onSlaveRead(data);
		}
	}

	virtual inline uint8 CallSlaveWrite() {
		if(onSlaveWrite != nullptr) {
			return onSlaveWrite();
		}
		return 0;
	}

	virtual inline void CallSlaveEndTransfer() {
		if(onSlaveEndTransfer != nullptr) {
			onSlaveEndTransfer();
		}
	}



	virtual inline void CallError(Error errorType) {
		if(onError != nullptr) {
			onError(errorType);
		}
	}
};















