#pragma once
#include <System/System.h>


// Base class to check for overlapping memory regions
template <typename RegisterMapType, typename... Registers>
struct RegisterMapOverlapChecker {
    // Check for overlapping memory regions
    template <typename Reg1, typename Reg2>
    static constexpr bool AreOverlapping() {
        constexpr uint32 start1 = Reg1::GetAddress();
        constexpr uint32 end1 = Reg1::GetAddress() + Reg1::GetDataTypeSize();
        constexpr uint32 start2 = Reg2::GetAddress();
        constexpr uint32 end2 = Reg2::GetAddress() + Reg2::GetDataTypeSize();
        return !(end1 <= start2 || end2 <= start1); // Regions overlap
    }

    // Recursive overlap check for all register pairs
    template <typename Reg1, typename... Remaining>
    static constexpr bool CheckOverlaps() {
        if constexpr (sizeof...(Remaining) == 0) {
            return true; // No more registers to check
        } else {
            return (... && !AreOverlapping<Reg1, Remaining>()) &&
                   CheckOverlaps<Remaining...>();
        }
    }

    // Run overlap check for all registers
    static_assert(
        CheckOverlaps<Registers...>(),
        "RegisterMap Checker Error: Memory regions of registers overlap"
    );
};


// Class for basic checks, including size and buffer checks
template <typename RegisterMapType, typename... Registers>
struct RegisterMapChecker : public RegisterMapOverlapChecker<RegisterMapType, Registers...> {
    static constexpr size_t totalSize = (Registers::GetDataTypeSize() + ...);
    static constexpr size_t mapSize = RegisterMapType::GetMapSize();
    static constexpr size_t bufferSize = RegisterMapType::GetBufferSize();

    // Check the total size of all registers
    static_assert(
        totalSize <= mapSize,
        "RegisterMap Checker Error: Total size of registers exceeds map size"
    );

    // Check the size of each register against the buffer size
    static_assert(
        (... && (Registers::GetDataTypeSize() <= bufferSize)),
        "RegisterMap Checker Error: One or more registers exceed buffer size"
    );

    // Check if any register exceeds the map size
    static_assert(
        (... && (Registers::GetAddress() + Registers::GetDataTypeSize() <= mapSize)),
        "RegisterMap Checker Error: One or more registers exceed the map size"
    );
};


// Strong checker class with stricter checks
template <typename RegisterMapType, typename... Registers>
struct RegisterMapCheckerStrong : public RegisterMapChecker<RegisterMapType, Registers...> {
    using Base = RegisterMapChecker<RegisterMapType, Registers...>;

    // Enforce strict alignment between total size and map size
    static_assert(
        Base::totalSize == Base::mapSize,
        "RegisterMap Checker Error: Total size of registers does not match map size"
    );

    // Enforce strict buffer size equality for all registers
    static_assert(
        (... && (Registers::GetDataTypeSize() == Base::bufferSize)),
        "RegisterMap Checker Error: One or more registers do not match buffer size"
    );
};
