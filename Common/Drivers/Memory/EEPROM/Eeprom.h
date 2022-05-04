#pragma once
#include <Periphery.h>
#include <Utilities/Status.h>
#include <functional>


namespace EEPROM {

	
	class Eeprom {
	private:
		I2C_HandleTypeDef *selfI2c = nullptr;
		uint8 i2cAddress = 0;
		uint16 addressPointer = 0;


	protected:
		enum class AddressSize : uint8 {ut8 = 1, u16 = 2};

		virtual uint16 GetMaxAddressPointer() = 0;
		virtual AddressSize GetMemoryAddressSize() = 0;



	public:
		std::function<bool()> i2cMutexTake;
		std::function<void()> i2cMutexGive;
	
	
	public:
		Eeprom() { }
	
	
		void SetI2c(I2C_HandleTypeDef &i2c) {
			selfI2c = &i2c;
		}


		void SetAddress(uint8 address) {
			i2cAddress = address;
		}


		Status::statusType IsMemoryReady() {
			if(!IsInit()) {
				return Status::noInit;
			}

			if(MutexTake()) {
				auto status = HAL_I2C_IsDeviceReady(selfI2c, i2cAddress, 10, 100);
				MutexGive();
				return Status::FromHal(status);
			}
			return Status::error;
		}


		Status::statusType ReadMemory(uint8 *dataOut, uint16 dataOutSize, uint16 memAddress) {
			if(!IsInit()) {
				return Status::noInit;
			}

			if(MutexTake()) {
				auto status = HAL_I2C_Mem_Read(selfI2c, i2cAddress, memAddress, (uint16)GetMemoryAddressSize(), dataOut, dataOutSize, 1000);
				MutexGive();
				return Status::FromHal(status);
			}
			return Status::error;
		}
	
	
		Status::statusType WriteMemory(uint8 *data, uint16 dataSize, uint16 memAddress) {
			if(!IsInit()) {
				return Status::noInit;
			}

			if(MutexTake()) {
				auto status = HAL_I2C_Mem_Write(selfI2c, i2cAddress, memAddress, (uint16)GetMemoryAddressSize(), data, dataSize, 1000);
				MutexGive();
				return Status::FromHal(status);
			}
			return Status::error;
		}

	
		Status::info<uint16> GetMemoryAddress(uint16 dataSize) {
			if(addressPointer + dataSize >= GetMaxAddressPointer()) {
				return {Status::outOfRange};
			}
			uint16 ret = addressPointer;
			addressPointer += dataSize;
			return {Status::ok, ret};
		}



	private:
		bool MutexTake() {
			if(i2cMutexTake == nullptr || i2cMutexGive == nullptr) {
				return true;
			}
			return i2cMutexTake();
		}

		void MutexGive() {
			if(i2cMutexTake == nullptr || i2cMutexGive == nullptr) {
				return;
			}
			i2cMutexGive();
		}

		bool IsInit() {
			return selfI2c != nullptr && i2cAddress != 0;
		}
	};



};
