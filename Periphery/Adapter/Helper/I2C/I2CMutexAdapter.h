#pragma once
#include "../../I2CAdapter.h"




template<
	class AdapterClass,
	typename = typename std::enable_if<std::is_base_of<I2CAdapter, AdapterClass>::value>::type
>
class I2CMutexAdapter: public AdapterClass {
public:
	std::function<void(bool lock)> onMutex;

public:
	I2CMutexAdapter() { }

	I2CMutexAdapter(I2C_TypeDef *i2c, uint32 busClockHz):AdapterClass(i2c, busClockHz) { }


	virtual Status::statusType CheckDevice(uint8 deviceAddress, uint16 repeat = 1) override {
		CallMutex(true);
		auto status = AdapterClass::CheckDevice(deviceAddress, repeat);
		CallMutex(false);
		return status;
	};



	virtual Status::statusType CheckDeviceAsync(uint8 deviceAddress, uint16 repeat = 1) override {
		CallMutex(true);
		auto status = AdapterClass::CheckDeviceAsync(deviceAddress, repeat);
		StatusAssert(status);
		status = AdapterClass::Await();
		CallMutex(false);
		return status;
	}



	virtual Status::info<uint8> Scan(uint8 *listBuffer, uint8 size) override {
		CallMutex(true);
		auto status = AdapterClass::Scan(listBuffer, size);
		CallMutex(false);
		return status;
	}



	virtual Status::info<uint8> ScanAsync(uint8 *listBuffer, uint8 size) override {
		CallMutex(true);
		auto status = AdapterClass::ScanAsync(listBuffer, size);
		if(status.IsError()) {
			return status;
		}
		status.type = AdapterClass::Await();
		CallMutex(false);
		return status;
	}



	virtual Status::statusType WriteByteArray(uint8 device, uint16 address, uint8 addressSize, uint8* writeData, uint32 dataSize) override {
		CallMutex(true);
		auto status = AdapterClass::WriteByteArray(device, address, addressSize, writeData, dataSize);
		CallMutex(false);
		return status;
	}



	virtual Status::statusType ReadByteArray(uint8 device, uint16 address, uint8 addressSize, uint8* readData, uint32 dataSize) override {
		CallMutex(true);
		auto status = AdapterClass::ReadByteArray(device, address, addressSize, readData, dataSize);
		CallMutex(false);
		return status;
	}



	virtual Status::statusType WriteByteArrayAsync(uint8 device, uint16 address, uint8 addressSize, uint8* writeData, uint32 dataSize) override {
		CallMutex(true);
		auto status = AdapterClass::WriteByteArrayAsync(device, address, addressSize, writeData, dataSize);
		StatusAssert(status);
		status = AdapterClass::Await();
		CallMutex(false);
		return status;
	}



	virtual Status::statusType ReadByteArrayAsync(uint8 device, uint16 address, uint8 addressSize, uint8* readData, uint32 dataSize) override {
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















