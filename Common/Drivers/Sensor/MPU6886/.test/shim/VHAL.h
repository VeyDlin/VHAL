#pragma once
// Host stand-in for the bus and timing part of the VHAL umbrella header, for the MPU6886 host tests. System only needs the delay.
// AI2C is a simulated device: a register file, a log of the writes, a FIFO that the FIFO data register reads from, and two
// faults a test can switch on: a register whose writes are dropped (so the readback check has something to catch), and a gyroscope
// X value that alternates between two levels on every burst read (so the bias calibration sees motion).
#include <Utilities/DataTypes.h>
#include <Utilities/Result/Result.h>
#include <Utilities/Result/ResultStatus.h>
#include <cstdint>
#include <utility>
#include <vector>


struct System {
	static void DelayMs(uint32 ms) {
		(void)ms;
	}
};


class AI2C {
public:
	uint8 regs[256] = {};
	// Writes to this register are logged but dropped, so the driver's readback check has something to catch.
	uint8 droppedWriteReg = 0xFF;
	// When set, the gyroscope X value of every burst read alternates between 0 and 62.5 deg/s (raw 0x1000).
	bool swingGyroX = false;
	uint32 burstReads = 0;
	// Bytes waiting in the FIFO; reads of FIFO_R_W (0x74) take them from the front.
	std::vector<uint8> fifo;
	std::vector<std::pair<uint8, uint8>> writes;


	template <typename DataType, typename AddressType>
	ResultStatus Write(uint8 device, AddressType address, DataType data) {
		(void)device;
		uint8 reg = static_cast<uint8>(address);
		uint8 value = static_cast<uint8>(data);
		writes.push_back(std::make_pair(reg, value));
		if (reg == droppedWriteReg) {
			return ResultStatus::ok;
		}

		// USER_CTRL.FIFO_RST (bit 2) clears the FIFO and clears itself.
		if (reg == 0x6A && (value & 0x04) != 0) {
			fifo.clear();
		}

		// DEVICE_RESET (bit 7 of PWR_MGMT_1) clears itself.
		bool selfClearing = reg == 0x6B && (value & 0x80) != 0;
		regs[reg] = selfClearing ? 0x00 : (reg == 0x6A ? static_cast<uint8>(value & ~0x04) : value);
		return ResultStatus::ok;
	}


	template <typename DataType, typename AddressType>
	Result<DataType> Read(uint8 device, AddressType address, uint32 size = 1) {
		(void)device;
		(void)size;
		uint8 reg = static_cast<uint8>(address);
		if (reg == 0x72) {
			return Result<DataType>(static_cast<DataType>(fifo.size() >> 8));
		}
		if (reg == 0x73) {
			return Result<DataType>(static_cast<DataType>(fifo.size() & 0xFF));
		}

		uint8 value = regs[reg];
		// INT_STATUS (0x3A) is read-to-clear.
		if (reg == 0x3A) {
			regs[reg] = 0x00;
		}
		return Result<DataType>(static_cast<DataType>(value));
	}


	template <typename DataType, typename AddressType>
	ResultStatus ReadArray(uint8 device, AddressType address, DataType* buffer, uint32 size = 1) {
		(void)device;
		uint8 start = static_cast<uint8>(address);
		if (start == 0x74) {
			for (uint32 index = 0; index < size; index++) {
				buffer[index] = fifo.empty() ? 0xFF : fifo.front();
				if (!fifo.empty()) {
					fifo.erase(fifo.begin());
				}
			}
			return ResultStatus::ok;
		}

		for (uint32 index = 0; index < size; index++) {
			buffer[index] = regs[static_cast<uint8>(start + index)];
		}

		if (swingGyroX) {
			bool high = (burstReads % 2) == 1;
			buffer[8] = high ? 0x10 : 0x00;
			buffer[9] = 0x00;
		}
		burstReads++;
		return ResultStatus::ok;
	}
};
