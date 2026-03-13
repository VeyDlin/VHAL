#pragma once
#include "IAdapter.h"

#define VHAL_I2C_ADAPTER


template<typename HandleType>
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
	HandleType *i2cHandle;
	Parameters parameters;

	uint32 inputBusClockHz = 0;

	uint32 timeout = 1000;

	ResultStatus state = ResultStatus::ready;

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
	std::function<void()> onComplete;
	// TODO: [VHAL] [I2C] [ADAPTER] [ADD] Add CheckDevice and ScanAsync


public:
	I2CAdapter() = default;

	I2CAdapter(HandleType *i2c, uint32 busClockHz):i2cHandle(i2c), inputBusClockHz(busClockHz) { }


	virtual ResultStatus SetParameters(Parameters val) {
		parameters = val;
		return Initialization();
	}


	virtual void SetTimeout(uint32 time) {
		timeout = time;
	}


	virtual ResultStatus Await() {
		// TODO: [VHAL] [I2C] [ADAPTER] [ADD] add slave listen
		// TODO: [VHAL] [I2C] [ADAPTER] [ADD]  add timeout
		while(state != ResultStatus::ready && state != ResultStatus::error);

		if (state == ResultStatus::ready) {
			return ResultStatus::ok;
		}
		return ResultStatus::error;
	}


	ResultStatus SetSlaveLiisten(bool mode) {
		auto status = mode ? StartSlaveListen() : StopSlaveListen();
		// TODO: [VHAL] [I2C] [ADAPTER] [WTF] add?
		//if(status == ResultStatus::ok) {
		//	continuousAsyncRxMode = mode;
		//}
		return status;
	}



	virtual ResultStatus CheckDevice(uint8 deviceAddress, uint16 repeat = 1) = 0;
	virtual ResultStatus CheckDeviceAsync(uint8 deviceAddress, uint16 repeat = 1) = 0;

	virtual Result<uint8> Scan(uint8 *listBuffer, uint8 size) = 0;
	virtual Result<uint8> ScanAsync(uint8 *listBuffer, uint8 size) = 0;

	virtual void IrqEventHandler() = 0;
	virtual void IrqErrorHandler() = 0;

	virtual ResultStatus WriteByteArray(uint8 device, uint16 address, uint8 addressSize, uint8* writeData, uint32 dataSize) = 0;
	virtual ResultStatus ReadByteArray(uint8 device, uint16 address, uint8 addressSize, uint8* readData, uint32 dataSize) = 0;

	virtual ResultStatus WriteByteArrayAsync(uint8 device, uint16 address, uint8 addressSize, uint8* writeData, uint32 dataSize) = 0;
	virtual ResultStatus ReadByteArrayAsync(uint8 device, uint16 address, uint8 addressSize, uint8* readData, uint32 dataSize) = 0;



	template <typename DataType, typename AddressType>
	inline ResultStatus Write(uint8 device, AddressType address, DataType data) {
		return WriteByteArray(device, address, sizeof(AddressType), reinterpret_cast<uint8*>(&data), sizeof(DataType));
	}


	template <typename DataType, typename AddressType>
	inline ResultStatus WriteArray(uint8 device, AddressType address, DataType* buffer, uint32 size) {
		return WriteByteArray(device, address, sizeof(AddressType), reinterpret_cast<uint8*>(buffer), sizeof(DataType) * size);
	}


	template <typename DataType, typename AddressType>
	inline ResultStatus WriteArray(uint8 device, AddressType address, const DataType* buffer, uint32 size) {
		return WriteByteArray(device, address, sizeof(AddressType), reinterpret_cast<uint8*>(buffer), sizeof(DataType) * size);
	}



	template <typename DataType, typename AddressType>
	inline ResultStatus ReadArray(uint8 device, AddressType address, DataType* buffer, uint32 size = 1) {
		return ReadByteArray(device, address, sizeof(AddressType), reinterpret_cast<uint8*>(buffer), sizeof(DataType) * size);
	}


	template <typename DataType, typename AddressType>
	inline Result<DataType> Read(uint8 device, AddressType address, uint32 size = 1) {
		DataType data;
		return Result<DataType>::Capture(
			ReadByteArray(device, address, sizeof(AddressType), reinterpret_cast<uint8*>(&data), sizeof(DataType) * size), data
		);
	}



	// ---------------



	template <typename DataType, typename AddressType>
	inline ResultStatus WriteAsync(uint8 device, AddressType address, DataType data) {
		return WriteByteArrayAsync(device, address, sizeof(AddressType), reinterpret_cast<uint8*>(&data), sizeof(DataType));
	}


	template <typename DataType, typename AddressType>
	inline ResultStatus WriteArrayAsync(uint8 device, AddressType address, DataType* buffer, uint32 size) {
		return WriteByteArrayAsync(device, address, sizeof(AddressType), reinterpret_cast<uint8*>(buffer), sizeof(DataType) * size);
	}


	template <typename DataType, typename AddressType>
	inline ResultStatus WriteArrayAsync(uint8 device, AddressType address, const DataType* buffer, uint32 size) {
		return WriteByteArrayAsync(device, address, sizeof(AddressType), reinterpret_cast<uint8*>(buffer), sizeof(DataType) * size);
	}



	template <typename DataType, typename AddressType>
	inline ResultStatus ReadArrayAsync(uint8 device, AddressType address, DataType* buffer, uint32 size = 1) {
		return ReadByteArrayAsync(device, address, sizeof(AddressType), reinterpret_cast<uint8*>(buffer), sizeof(DataType) * size);
	}


	template <typename DataType, typename AddressType>
	inline Result<DataType> ReadAsync(uint8 device, AddressType address, uint32 size = 1) {
		DataType data;
		return Result<DataType>::Capture(
			ReadByteArrayAsync(device, address, sizeof(AddressType), reinterpret_cast<uint8*>(&data), sizeof(DataType) * size), data
		);
	}


protected:
	virtual ResultStatus Initialization() = 0;


	virtual ResultStatus StartSlaveListen() {
		return ResultStatus::notSupported;
	}

	virtual ResultStatus StopSlaveListen() {
		return ResultStatus::notSupported;
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















