#pragma once
// Desktop stand-in for VHAL.h: the platform pieces the bootloader needs (System, an in-memory NOR flash as AFLASH).
// The bootloader and the streaming protocol compiled with it are the real VHAL sources.
#include <Utilities/DataTypes.h>
#include <Utilities/Result/Result.h>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>


struct System {
    static inline const std::chrono::steady_clock::time_point startTime = std::chrono::steady_clock::now();
    static inline bool resetRequested = false;


    static void CriticalSection(bool isEnable) {
        (void)isEnable;
    }


    static uint64 GetMs() {
        std::chrono::steady_clock::duration elapsed = std::chrono::steady_clock::now() - startTime;
        return static_cast<uint64>(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
    }


    static uint64 GetTick() {
        return GetMs();
    }


    // The simulator main loop performs the reset (writes the flash image, exits) once the bootloader requested it
    static void Reset() {
        resetRequested = true;
    }


    static void Abort() {
        std::fputs("vboot-sim: System::Abort\n", stderr);
        std::abort();
    }
};


#define SystemAssert(condition, ...) \
    if (!(condition)) { \
        System::Abort(); \
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
