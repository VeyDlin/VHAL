#pragma once
#include <Utilities/DataTypes.h>
#include <Utilities/Result/ResultStatus.h>
#include <cstddef>
#include <vector>


// A fake I2C adapter with the public ReadArray and WriteArray of I2CAdapter. It keeps one memory for all devices, indexed by
// register address, and logs every call in order. failWith, when not ok, is returned and nothing moves.


struct FakeI2cTransaction {
	bool isRead;
	uint8 device;
	uint16 address;
	uint8 addressSize;
	std::vector<uint8> bytes;
};


struct FakeI2cAdapter {
	std::vector<uint8> memory = std::vector<uint8>(0x10100, 0);
	std::vector<FakeI2cTransaction> log;
	ResultStatus failWith = ResultStatus::ok;


	template<typename DataType, typename AddressType>
	ResultStatus WriteArray(uint8 device, AddressType address, const DataType* buffer, uint32 size) {
		FakeI2cTransaction transaction{ false, device, static_cast<uint16>(address), static_cast<uint8>(sizeof(AddressType)), std::vector<uint8>() };
		if (failWith != ResultStatus::ok) {
			log.push_back(transaction);
			return failWith;
		}

		for (uint32 index = 0; index < size; index++) {
			memory[static_cast<std::size_t>(address) + index] = buffer[index];
			transaction.bytes.push_back(buffer[index]);
		}
		log.push_back(transaction);
		return ResultStatus::ok;
	}


	template<typename DataType, typename AddressType>
	ResultStatus ReadArray(uint8 device, AddressType address, DataType* buffer, uint32 size = 1) {
		FakeI2cTransaction transaction{ true, device, static_cast<uint16>(address), static_cast<uint8>(sizeof(AddressType)), std::vector<uint8>() };
		if (failWith != ResultStatus::ok) {
			log.push_back(transaction);
			return failWith;
		}

		for (uint32 index = 0; index < size; index++) {
			buffer[index] = memory[static_cast<std::size_t>(address) + index];
			transaction.bytes.push_back(buffer[index]);
		}
		log.push_back(transaction);
		return ResultStatus::ok;
	}
};
