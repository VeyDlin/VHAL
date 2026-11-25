#pragma once
#include <Utilities/Checksum/CRC/CrcParameters.h>
#include <Utilities/Checksum/CRC/Engine/CrcStep.h>
#include <Utilities/DataTypes.h>
#include <span>

namespace Checksum {


// The nibble engine: one lookup in a 16-entry table per four bits, so two per byte. The table costs 16 entries of the width in flash
// for each algorithm. It takes widths of 4 bits and above.
struct CrcNibble {
	template<CrcParameters Parameters>
	static constexpr bool Supports() {
		return Parameters.width >= 4;
	}


	template<CrcParameters Parameters>
	static constexpr CrcRegister<Parameters.width> Run(CrcRegister<Parameters.width> start, std::span<const uint8> data) {
		return CrcRunStep<Parameters, 4>(start, data);
	}
};
}
