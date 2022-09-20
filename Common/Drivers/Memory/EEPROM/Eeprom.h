#pragma once
#include <BSP.h>
#include <functional>


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
				return { Status::outOfRange };
			}
			uint16 ret = addressPointer;
			addressPointer += dataSize;
			return { Status::ok, ret };
		}


	private:
		bool IsInit() {
			return i2c != nullptr && i2cAddress != 0;
		}
	};
};
