#pragma once
#include <Utilities/Checksum/CRC/CrcParameters.h>
#include <Utilities/Checksum/CRC/Engine/CrcStep.h>
#include <Utilities/DataTypes.h>
#include <span>

namespace Checksum {


// The table engine: one lookup in a 256-entry table per byte. The table costs 256 entries of the width in flash for each algorithm
// used. It takes widths of 8 bits and above.
struct CrcTable {
	template<CrcParameters Parameters>
	static constexpr bool Supports() {
		return Parameters.width >= 8;
	}


	template<CrcParameters Parameters>
	static constexpr CrcRegister<Parameters.width> Run(CrcRegister<Parameters.width> start, std::span<const uint8> data) {
		return CrcRunStep<Parameters, 8>(start, data);
	}
};
}
