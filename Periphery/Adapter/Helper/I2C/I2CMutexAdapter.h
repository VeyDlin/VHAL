#pragma once
#include <Adapter/I2CAdapter.h>


template<
	class AdapterClass
>
class I2CMutexAdapter: public AdapterClass {
public:
	std::function<void(bool lock)> onMutex;

public:
	I2CMutexAdapter() { }

	template<typename HandleT>
	I2CMutexAdapter(HandleT *i2c, uint32 busClockHz):AdapterClass(i2c, busClockHz) { }


	virtual ResultStatus CheckDevice(uint8 deviceAddress, uint16 repeat = 1) override {
		CallMutex(true);
		auto status = AdapterClass::CheckDevice(deviceAddress, repeat);
		CallMutex(false);
		return status;
	};



	virtual ResultStatus CheckDeviceAsync(uint8 deviceAddress, uint16 repeat = 1) override {
		CallMutex(true);
		auto status = AdapterClass::CheckDeviceAsync(deviceAddress, repeat);
		StatusAssert(status);
		status = AdapterClass::Await();
		CallMutex(false);
		return status;
	}



	virtual Result<uint8> Scan(uint8 *listBuffer, uint8 size) override {
		CallMutex(true);
		auto status = AdapterClass::Scan(listBuffer, size);
		CallMutex(false);
		return status;
	}



	virtual Result<uint8> ScanAsync(uint8 *listBuffer, uint8 size) override {
		CallMutex(true);
		auto status = AdapterClass::ScanAsync(listBuffer, size);
		if(status.IsErr()) {
			return status;
		}
		auto awaitStatus = AdapterClass::Await();
		if(awaitStatus != ResultStatus::ok) {
			return awaitStatus;
		}
		CallMutex(false);
		return status;
	}



	virtual ResultStatus WriteByteArray(uint8 device, uint16 address, uint8 addressSize, uint8* writeData, uint32 dataSize) override {
		CallMutex(true);
		auto status = AdapterClass::WriteByteArray(device, address, addressSize, writeData, dataSize);
		CallMutex(false);
		return status;
	}



	virtual ResultStatus ReadByteArray(uint8 device, uint16 address, uint8 addressSize, uint8* readData, uint32 dataSize) override {
		CallMutex(true);
		auto status = AdapterClass::ReadByteArray(device, address, addressSize, readData, dataSize);
		CallMutex(false);
		return status;
	}



	virtual ResultStatus WriteByteArrayAsync(uint8 device, uint16 address, uint8 addressSize, uint8* writeData, uint32 dataSize) override {
		CallMutex(true);
		auto status = AdapterClass::WriteByteArrayAsync(device, address, addressSize, writeData, dataSize);
		StatusAssert(status);
		status = AdapterClass::Await();
		CallMutex(false);
		return status;
	}



	virtual ResultStatus ReadByteArrayAsync(uint8 device, uint16 address, uint8 addressSize, uint8* readData, uint32 dataSize) override {
		CallMutex(true);
		auto status = AdapterClass::ReadByteArrayAsync(device, address, addressSize, readData, dataSize);
		StatusAssert(status);
		status = AdapterClass::Await();
		CallMutex(false);
		return status;
	}




protected:
	virtual void CallMutex(bool isLock) {
		if(onMutex) {
			onMutex(isLock);
		}
	}
};















