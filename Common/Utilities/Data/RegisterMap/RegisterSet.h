#pragma once
#include <Utilities/DataTypes.h>
#include <Utilities/Data/RegisterMap/RegisterDescriptor.h>
#include <array>
#include <cstddef>


// True when no two descriptors have the same table, device address and register address. Modbus tables are separate address spaces,
// so a holding register and a coil at one address are two registers.
template<auto... Regs>
constexpr bool RegisterSetIsUnique() {
	std::array<uint32, sizeof...(Regs)> keys{
		((static_cast<uint32>(Regs.table) << 24) | (static_cast<uint32>(Regs.device) << 16) | static_cast<uint32>(Regs.address))...
	};
	for (std::size_t first = 0; first < keys.size(); first++) {
		for (std::size_t second = first + 1; second < keys.size(); second++) {
			if (keys[first] == keys[second]) {
				return false;
			}
		}
	}
	return true;
}


// A list of register descriptors with the duplicate check. A bare list of descriptors has no check; declare the set as a
// constexpr object, or use it in a static_assert, so the check runs.
//
//   constexpr RegisterSet<ChargeControl, ChargeStatus> board{};
template<auto... Regs>
struct RegisterSet {
	static constexpr std::size_t count = sizeof...(Regs);
	static constexpr bool unique = RegisterSetIsUnique<Regs...>();


	static_assert(unique, "two registers share a table, device and address");
};
