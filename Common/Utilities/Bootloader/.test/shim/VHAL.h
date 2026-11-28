#pragma once
// Host stand-in for VHAL.h of the bootloader, used by the unit tests and by the device simulator (main.cpp). The System functions
// come from the shared VhalSystem.h (a simulated clock, Reset recorded in VhalSim::resetCalled). The flash is an in-memory NOR
// region, the AFLASH of the bootloader. The bootloader and the transport classes compiled with it are the real VHAL sources.
#include <VhalSystem.h>
#include <System/System.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/Result.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>


// Simulated NOR flash region. Programming can only clear bits (cell &= value), like real flash without an erase. A write is refused
// when its address or size is not a multiple of requiredAlignment, and when requireErasedForProgram is set and a target byte is not
// erased. Read-out protection removal mass-erases the flash, as on STM32 when RDP goes from level 1 to 0.
// failWrites, writeCalls and disableReadProtectionCalled are test hooks: the device simulator leaves them at their defaults.
class FakeFlash {
public:
	enum class FlashProtectionLevel {
		Level0,
		Level1,
		Level2,
		Unknown
	};

	struct Parameters {
		uint32 startAddress;
		uint32 endAddress;
	};

	uint32 base;
	std::vector<uint8> memory;
	Parameters parameters = {};
	FlashProtectionLevel level = FlashProtectionLevel::Level0;
	bool readProtected = false;

	// Like a word-programmed flash (STM32F4): addresses and sizes of a write must be multiples of this value.
	uint32 requiredAlignment = 1;

	// Like STM32G0/G4: a write is refused when a target byte is not erased, so a word is programmed once.
	bool requireErasedForProgram = false;

	// The next failWrites writes fail; writeCalls counts every WriteData call.
	int failWrites = 0;
	uint32 writeCalls = 0;
	bool disableReadProtectionCalled = false;


	FakeFlash(uint32 baseAddress, uint32 size) : base(baseAddress), memory(size, 0xFF) { }


	ResultStatus SetParameters(const Parameters& params) {
		parameters = params;
		return ResultStatus::ok;
	}


	ResultStatus Unlock() {
		return ResultStatus::ok;
	}


	ResultStatus Lock() {
		return ResultStatus::ok;
	}


	Result<uint8> Read(uint8* address) {
		uintptr_t value = reinterpret_cast<uintptr_t>(address);

		if (value < base || value - base >= memory.size()) {
			return ResultStatus::badAddress;
		}

		return memory[value - base];
	}


	ResultStatus WriteData(uint32* address, const void* data, size_t size) {
		writeCalls++;

		if (failWrites > 0) {
			failWrites--;
			return ResultStatus::writeError;
		}

		uintptr_t value = reinterpret_cast<uintptr_t>(address);

		if (value < base || value - base + size > memory.size()) {
			return ResultStatus::badAddress;
		}

		if (value % requiredAlignment != 0 || size % requiredAlignment != 0) {
			return ResultStatus::writeError;
		}

		size_t offset = static_cast<size_t>(value - base);

		if (requireErasedForProgram) {
			for (size_t i = 0; i < size; i++) {
				if (memory[offset + i] != 0xFF) {
					return ResultStatus::writeError;
				}
			}
		}

		const uint8* source = static_cast<const uint8*>(data);

		for (size_t i = 0; i < size; i++) {
			memory[offset + i] = static_cast<uint8>(memory[offset + i] & source[i]);
		}

		return ResultStatus::ok;
	}


	bool IsReadProtected() const {
		return readProtected;
	}


	FlashProtectionLevel GetProtectionLevel() const {
		return level;
	}


	ResultStatus DisableReadProtection() {
		disableReadProtectionCalled = true;
		std::fill(memory.begin(), memory.end(), 0xFF);
		readProtected = false;
		level = FlashProtectionLevel::Level0;
		return ResultStatus::ok;
	}


	ResultStatus Erase(uint32 address, uint32 size) {
		if (address < base || static_cast<size_t>(address - base) + size > memory.size()) {
			return ResultStatus::badAddress;
		}

		std::fill_n(memory.begin() + static_cast<size_t>(address - base), size, 0xFF);
		return ResultStatus::ok;
	}
};


using AFLASH = FakeFlash;

// The device simulator names its flash SimFlash.
using SimFlash = FakeFlash;
