#pragma once
#include <Utilities/Checksum/CRC/CrcParameters.h>
#include <Utilities/DataTypes.h>
#include <array>
#include <cstddef>
#include <span>

namespace Checksum {


// The table of the 2^Bits register values of one step: entry i is the register after Bits steps of zero input, started from the
// value i. The reflected form starts from i in the low bits; the normal form starts from i in the top Bits bits, and the entry is the
// CRC value of the width, so the table takes width bits per entry. The table engines use it with Bits 4 (nibble) and 8 (byte).
template<CrcParameters Parameters, uint8 Bits>
constexpr std::array<CrcValue<Parameters.width>, (std::size_t(1) << Bits)> CrcMakeStepTable() {
	using Register = CrcRegister<Parameters.width>;
	constexpr uint8 registerBits = sizeof(Register) * 8;
	constexpr uint8 offset = registerBits - Parameters.width;
	std::array<CrcValue<Parameters.width>, (std::size_t(1) << Bits)> table{};
	for (std::size_t index = 0; index < table.size(); index++) {
		if constexpr (Parameters.reflectIn) {
			constexpr Register polynomial = static_cast<Register>(CrcReverse(Parameters.polynomial, Parameters.width));
			Register remainder = static_cast<Register>(index);
			for (uint8 step = 0; step < Bits; step++) {
				const bool feedback = (remainder & 1) != 0;
				remainder >>= 1;
				if (feedback) {
					remainder ^= polynomial;
				}
			}

			table[index] = static_cast<CrcValue<Parameters.width>>(remainder);
		} else {
			constexpr Register polynomial = static_cast<Register>(Parameters.polynomial << offset);
			Register remainder = static_cast<Register>(index << (registerBits - Bits));
			for (uint8 step = 0; step < Bits; step++) {
				const bool feedback = ((remainder >> (registerBits - 1)) & 1) != 0;
				remainder <<= 1;
				if (feedback) {
					remainder ^= polynomial;
				}
			}

			table[index] = static_cast<CrcValue<Parameters.width>>(remainder >> offset);
		}
	}

	return table;
}


// The table of one algorithm and step size, as flash data.
template<CrcParameters Parameters, uint8 Bits>
struct CrcStepTableOf {
	static constexpr std::array<CrcValue<Parameters.width>, (std::size_t(1) << Bits)> table = CrcMakeStepTable<Parameters, Bits>();
};


// The table engine's loop, for a step of Bits bits: a byte takes 8 / Bits lookups, the low chunk first for a reflected input and the top
// chunk first for a normal one. The register is in the form of CrcStart; the table entry is shifted into the top of the register for a
// normal input, and the register's top Bits bits are the index.
template<CrcParameters Parameters, uint8 Bits>
constexpr CrcRegister<Parameters.width> CrcRunStep(CrcRegister<Parameters.width> start, std::span<const uint8> data) {
	using Register = CrcRegister<Parameters.width>;
	constexpr uint8 registerBits = sizeof(Register) * 8;
	constexpr uint8 offset = registerBits - Parameters.width;
	constexpr uint32 mask = (1u << Bits) - 1;
	constexpr uint8 chunks = 8 / Bits;
	Register remainder = start;
	for (uint8 byte : data) {
		for (uint8 chunk = 0; chunk < chunks; chunk++) {
			if constexpr (Parameters.reflectIn) {
				const uint32 part = (byte >> (chunk * Bits)) & mask;
				const uint32 index = (remainder ^ part) & mask;
				remainder = static_cast<Register>(CrcStepTableOf<Parameters, Bits>::table[index]) ^ (remainder >> Bits);
			} else {
				const uint32 part = (byte >> ((chunks - 1 - chunk) * Bits)) & mask;
				const uint32 index = ((remainder >> (registerBits - Bits)) ^ part) & mask;
				remainder = (remainder << Bits) ^ (static_cast<Register>(CrcStepTableOf<Parameters, Bits>::table[index]) << offset);
			}
		}
	}

	return remainder;
}
}
