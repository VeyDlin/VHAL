#pragma once
// Simulator stand-in for VHAL.h: the platform pieces the bootloader needs (the System functions, an in-memory NOR flash as AFLASH).
// The real System declaration is kept, so the headers that include System/System.h compile unchanged, and the functions the
// bootloader reaches are defined here. The bootloader and the streaming protocol compiled with it are the real VHAL sources.
#include <System/System.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/Result.h>
#include <algorithm>
#include <chrono>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>


namespace VbootSim {


// The clock starts when the simulator starts, and the simulator main loop performs the reset (writes the flash image, exits)
// once the bootloader requested it.
inline const std::chrono::steady_clock::time_point startTime = std::chrono::steady_clock::now();
inline bool resetRequested = false;


}


inline void System::CriticalSection(bool isEnable) {
	(void)isEnable;
}


inline uint64 System::GetMs() {
	std::chrono::steady_clock::duration elapsed = std::chrono::steady_clock::now() - VbootSim::startTime;
	return static_cast<uint64>(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
}


inline uint64 System::GetTick() {
	return GetMs();
}


inline void System::Reset() {
	VbootSim::resetRequested = true;
}


inline void System::Abort(const char* message, const char* file, uint32 line) {
	std::fprintf(stderr, "vboot-sim: System::Abort: %s (%s:%" PRIu32 ")\n", message != nullptr ? message : "no message", file != nullptr ? file : "?", line);
	std::abort();
}


inline void System::CriticalError(const char* message, const char* file, uint32 line) {
	System::Abort(message, file, line);
}


// NOR flash region: programming can only clear bits (cell &= value), an erase sets the bytes to 0xFF.
class SimFlash {
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

    uint32 base = 0;
    std::vector<uint8> memory;
    Parameters parameters{};
    FlashProtectionLevel level = FlashProtectionLevel::Level0;
    bool readProtected = false;

    // Like a word-programmed flash: addresses and sizes of a write must be multiples of this value
    uint32 requiredAlignment = 1;

    // Like STM32G0/G4: a write is refused when a target byte is not erased, so a word is programmed once
    bool requireErasedForProgram = false;


    SimFlash(uint32 baseAddress, uint32 size) : base(baseAddress), memory(size, 0xFF) { }


    ResultStatus SetParameters(const Parameters& params) {
        parameters = params;
        return ResultStatus::ok;
    }


    ResultStatus Unlock() {
        return ResultStatus::ok;
    }


    void Lock() {
    }


    Result<uint8> Read(uint8* address) {
        uintptr_t value = reinterpret_cast<uintptr_t>(address);

        if (value < base || value - base >= memory.size()) {
            return ResultStatus::badAddress;
        }

        return memory[value - base];
    }


    ResultStatus WriteData(uint32* address, const void* data, size_t size) {
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


    // Read-out protection removal mass-erases the flash, as on STM32 when RDP goes from level 1 to 0
    ResultStatus DisableReadProtection() {
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


using AFLASH = SimFlash;
