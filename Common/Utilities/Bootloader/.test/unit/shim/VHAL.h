#pragma once
// Host stand-in for VHAL.h for the bootloader's unit tests: the real System declaration, a simulated clock that the tests move, a
// Reset that only records the call, and an in-memory NOR flash as AFLASH. The bootloader and the transport classes compiled with it
// are the real VHAL sources.
#include <System/System.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/Result.h>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <vector>


namespace VhalSim {


// The simulated clock, in milliseconds, and whether System::Reset was called.
inline uint64 clockMs = 0;
inline bool resetCalled = false;


// Sets the clock. The 32-bit tick is not used by the bootloader tests, so it is not kept.
inline void SetMs(uint64 value) {
	clockMs = value;
}


// Moves the clock forward by delta milliseconds.
inline void AdvanceMs(uint32 delta) {
	SetMs(clockMs + delta);
}


}


inline void System::CriticalSection(bool isEnable) {
	(void)isEnable;
}


inline uint64 System::GetTick() {
	return VhalSim::clockMs;
}


inline uint64 System::GetMs() {
	return VhalSim::clockMs;
}


inline void System::Reset() {
	VhalSim::resetCalled = true;
}


inline void System::Abort(const char* message, const char* file, uint32 line) {
	std::fprintf(stderr, "System::Abort: %s (%s:%" PRIu32 ")\n", message != nullptr ? message : "no message", file != nullptr ? file : "?", line);
	std::abort();
}


inline void System::CriticalError(const char* message, const char* file, uint32 line) {
	System::Abort(message, file, line);
}


// Simulated NOR flash region. Programming can only clear bits (cell &= value), like real flash without an erase.
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
	bool disableReadProtectionCalled = false;
	int failWrites = 0;
	uint32 writeCalls = 0;
	// Like a word-programmed flash (STM32F4): WriteData fails unless address and size are multiples of this value.
	uint32 requiredAlignment = 1;
	// Like STM32G0/G4: a write is refused when a target byte is not erased (0xFF), so a second program of a word fails.
	bool requireErasedForProgram = false;


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
		if (value < base || value >= base + memory.size()) {
			return ResultStatus::error;
		}
		return memory[value - base];
	}


	ResultStatus WriteData(uint32* address, const void* data, size_t size) {
		writeCalls++;
		if (failWrites > 0) {
			failWrites--;
			return ResultStatus::error;
		}

		uintptr_t value = reinterpret_cast<uintptr_t>(address);
		if (value < base || value + size > base + memory.size()) {
			return ResultStatus::error;
		}

		if (value % requiredAlignment != 0 || size % requiredAlignment != 0) {
			return ResultStatus::error;
		}

		if (requireErasedForProgram) {
			for (size_t i = 0; i < size; i++) {
				if (memory[value - base + i] != 0xFF) {
					return ResultStatus::error;
				}
			}
		}

		const uint8* source = static_cast<const uint8*>(data);
		for (size_t i = 0; i < size; i++) {
			memory[value - base + i] &= source[i];
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
		return ResultStatus::ok;
	}


	void Erase(uint32 address, uint32 size) {
		for (uint32 i = 0; i < size; i++) {
			memory[address - base + i] = 0xFF;
		}
	}
};


using AFLASH = FakeFlash;
