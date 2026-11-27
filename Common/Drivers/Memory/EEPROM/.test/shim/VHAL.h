#pragma once
// Desktop stand-in for the VHAL umbrella header, for the EEPROM host tests. The I2C adapter is a fake bus with one 24xx chip
// model behind it, and System gives a fake clock for the ACK polling. The bus takes 7-bit addresses: an 8-bit control byte
// such as 0xA0 is refused with badAddress, as the ESP32 port refuses it (the F4 port does not). The chip model follows the
// datasheet: a write cycle that NACKs while it is busy, and a write that stays inside its page.
#include <Utilities/DataTypes.h>
#include <Utilities/Result/Result.h>
#include <cstdint>
#include <cstdio>
#include <vector>


// The clock of the simulation, in milliseconds. DelayMs advances it, so a busy chip becomes ready while the driver polls.
inline uint64 simNowMs = 0;


class System {
public:
	static uint64 GetMs();
	static void DelayMs(uint32 delay);
};


inline uint64 System::GetMs() {
	return simNowMs;
}


inline void System::DelayMs(uint32 delay) {
	simNowMs += delay;
}


// A serial EEPROM of the 24xx family. The capacity, page size and address bytes are the datasheet values of the chip.
class Chip24xx {
public:
	uint8 address7 = 0;
	uint32 capacity = 0;
	uint16 pageSize = 0;
	uint8 addressBytes = 0;
	uint64 writeCycleMs = 0;
	uint64 busyUntilMs = 0;
	uint32 acceptedWrites = 0;
	std::vector<uint8> memory;


	Chip24xx(uint8 chipAddress7, uint32 chipCapacity, uint16 chipPageSize, uint8 chipAddressBytes, uint64 chipWriteCycleMs):
		address7(chipAddress7),
		capacity(chipCapacity),
		pageSize(chipPageSize),
		addressBytes(chipAddressBytes),
		writeCycleMs(chipWriteCycleMs),
		memory(chipCapacity, 0xFF)
	{ }


	bool IsBusy() const {
		return System::GetMs() < busyUntilMs;
	}


	// Programs a write that starts at address. Bytes past the end of the page wrap to the start of the same page, as a
	// 24xx chip does, and the write cycle starts when the stop condition arrives.
	void Program(uint32 address, const uint8 *data, uint32 size) {
		uint32 start = address % capacity;
		uint32 offset = start % pageSize;
		uint32 pageStart = start - offset;

		for (uint32 i = 0; i < size; i++) {
			memory[pageStart + (offset + i) % pageSize] = data[i];
		}

		acceptedWrites++;
		busyUntilMs = System::GetMs() + writeCycleMs;
	}
};


// The I2C adapter. It forwards each call to the attached chip, and it NACKs a call while the chip is busy, as the bus does.
class AI2C {
public:
	Chip24xx *chip = nullptr;
	uint8 lastDevice = 0;
	uint8 lastAddressBytes = 0;
	uint32 probeCount = 0;
	uint32 writeCallCount = 0;
	uint32 readCallCount = 0;
	// Transfers NACK this many times, for the retry tests. A probe does not use up a forced NACK.
	int forcedNacks = 0;


	AI2C() = default;


	explicit AI2C(Chip24xx &attached): chip(&attached) { }


	ResultStatus CheckDevice(uint8 device, uint16 /*repeat*/ = 1) {
		probeCount++;
		lastDevice = device;

		if (device >= 128) {
			return ResultStatus::badAddress;
		}

		return Select(device, 0, false);
	}


	ResultStatus ReadByteArray(uint8 device, uint16 address, uint8 addressSize, uint8 *data, uint32 size) {
		readCallCount++;
		lastDevice = device;
		lastAddressBytes = addressSize;

		if (device >= 128) {
			return ResultStatus::badAddress;
		}

		ResultStatus status = Select(device, addressSize, true);
		if (status != ResultStatus::ok) {
			return status;
		}

		for (uint32 i = 0; i < size; i++) {
			data[i] = chip->memory[(address + i) % chip->capacity];
		}

		return ResultStatus::ok;
	}


	ResultStatus WriteByteArray(uint8 device, uint16 address, uint8 addressSize, uint8 *data, uint32 size) {
		writeCallCount++;
		lastDevice = device;
		lastAddressBytes = addressSize;

		if (device >= 128) {
			return ResultStatus::badAddress;
		}

		ResultStatus status = Select(device, addressSize, true);
		if (status != ResultStatus::ok) {
			return status;
		}

		chip->Program(address, data, size);
		return ResultStatus::ok;
	}


private:
	// The address phase: the chip must be attached, must answer to the device, must be ready, and must take the address bytes it has.
	ResultStatus Select(uint8 device, uint8 addressSize, bool transfer) {
		if (chip == nullptr || device != chip->address7) {
			return ResultStatus::nack;
		}

		if (transfer && forcedNacks > 0) {
			forcedNacks--;
			return ResultStatus::nack;
		}

		if (chip->IsBusy()) {
			return ResultStatus::nack;
		}

		if (addressSize != 0 && addressSize != chip->addressBytes) {
			return ResultStatus::invalidParameter;
		}

		return ResultStatus::ok;
	}
};
