#pragma once
#include <BSP.h>
#include <functional>


/*
	#include <AT24C256C.h>

	struct MyData {
		int id;
		float value;
		char name[20];
	} _APacked;

	// Initialize your I2C driver
	AI2C i2cDriver; // Replace this with your actual AI2C initialization

	// Initialize the AT24C256C EEPROM
	EEPROM::AT24C256C eeprom(i2cDriver);
	eeprom.retry = 3;

	// Check if the EEPROM is ready
	if (eeprom.IsMemoryReady() != Status::ok) {
		System::console << Console::error << "EEPROM is not ready!" << Console::endl;
		return;
	}

	// Create a Data object for storing MyData
	EEPROM::Data<MyData> myDataStorage(eeprom);
	EEPROM::Data<float> myFloatStorage(eeprom);

	// Write data to EEPROM
	myDataStorage = { 1, 42.42, "TestName" };
	myFloatStorage = 1.1234;


	// Or write data to EEPROM with status
	MyData writeData = { 1, 42.42, "TestName" };
	if (myDataStorage.Set(writeData) != Status::ok) {
		System::console << Console::error << "Failed to write myDataStorage to EEPROM!" << Console::endl;
	}

	if (myFloatStorage.Set(1.1234) != Status::ok) {
		System::console << Console::error << "Failed to write myFloatStorage to EEPROM!" << Console::endl;
	}


	// Read data back from EEPROM
	auto dataStorage = myDataStorage;
	auto floatStorage = myFloatStorage;


	// Or read data back from EEPROM with status
	auto dataStatus = myDataStorage.Get();
	if (!dataStatus.IsOk()) {
		System::console << Console::error << "Failed to read myDataStorage from EEPROM!" << Console::endl;
	}

	auto floatStatus = myFloatStorage.Get();
	if (!floatStatus.IsOk()) {
		System::console << Console::error << "Failed to read myFloatStorage from EEPROM!" << Console::endl;
	}
*/


namespace EEPROM {
	class Eeprom {
	private:
		AI2C *i2c;
		uint8 i2cAddress = 0;
		uint16 addressPointer = 0;

	protected:
		enum class AddressSize : uint8 { U8 = 1, U16 = 2 };

		virtual uint16 GetMaxAddressPointer() = 0;
		virtual AddressSize GetMemoryAddressSize() = 0;

	public:
		uint8 retry = 1;

	public:
		Eeprom() { }


		Eeprom(AI2C &_i2c): i2c(_i2c) { }


		Eeprom(AI2C &_i2c, uint8 address): i2c(_i2c), i2cAddress(address) { }


		void SetAddress(uint8 address) {
			i2cAddress = address;
		}


		Status::statusType IsMemoryReady() {
			if(!IsInit()) {
				return Status::noInit;
			}

			return i2c->CheckDevice(i2cAddress, 10);
		}


		Status::statusType ReadMemory(uint8 *dataOut, uint16 dataOutSize, uint16 memAddress) {
			if(!IsInit()) {
				return Status::noInit;
			}
			return i2c->ReadByteArray(i2cAddress, memAddress, GetMemoryAddressSize(), dataOut, dataOutSize);
		}
	
	
		Status::statusType WriteMemory(uint8 *data, uint16 dataSize, uint16 memAddress) {
			if(!IsInit()) {
				return Status::noInit;
			}
			return i2c->WriteByteArray(i2cAddress, memAddress, GetMemoryAddressSize(), data, dataSize);
		}

	
		Status::info<uint16> GetMemoryAddress(uint16 dataSize) {
			if(addressPointer + dataSize >= GetMaxAddressPointer()) {
				return Status::outOfRange;
			}
			uint16 ret = addressPointer;
			addressPointer += dataSize;
			return ret;
		}


	private:
		bool IsInit() {
			return i2c != nullptr && i2cAddress != 0;
		}
	};
};
