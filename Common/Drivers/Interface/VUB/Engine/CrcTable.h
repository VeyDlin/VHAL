#pragma once
#include <Drivers/Interface/VUB/Engine/CrcCore.h>
#include <Utilities/DataTypes.h>
#include <array>
#include <cstddef>
#include <span>
#include <type_traits>

namespace VUB {


// The register type of a CRC of width bits: a table entry takes the width in bytes, so the table costs 256 times that many bytes of flash.
template<uint8 Width>
using CrcWord = std::conditional_t<Width == 8, uint8, std::conditional_t<Width == 16, uint16, uint32>>;


// The 256-entry table of an algorithm, built at compile time. The reflected form (for a reflected input) indexes by the low byte of the
// register and uses the reflected polynomial; the normal form indexes by the top byte.
template<CrcParameters Parameters>
consteval std::array<CrcWord<Parameters.width>, 256> CrcMakeTable() {
	using Word = CrcWord<Parameters.width>;
	const uint32 mask = CrcMask(Parameters.width);
	const uint32 polynomial = Parameters.reflectIn ? CrcReverse(Parameters.polynomial, Parameters.width) : Parameters.polynomial;
	const uint32 top = 1u << (Parameters.width - 1);
	std::array<Word, 256> table{};
	for (uint32 index = 0; index < 256; index++) {
		uint32 entry = 0;
		if (Parameters.reflectIn) {
			entry = index;
			for (uint8 bit = 0; bit < 8; bit++) {
				entry = (entry & 1u) != 0 ? (entry >> 1) ^ polynomial : entry >> 1;
			}
		} else {
			entry = index << (Parameters.width - 8);
			for (uint8 bit = 0; bit < 8; bit++) {
				entry = (entry & top) != 0 ? ((entry << 1) ^ polynomial) & mask : (entry << 1) & mask;
			}
		}

		table[index] = static_cast<Word>(entry);
	}

	return table;
}


// The table of one algorithm, as flash data. The engine reads it through table.data().
template<CrcParameters Parameters>
struct CrcTableOf {
	static constexpr std::array<CrcWord<Parameters.width>, 256> table = CrcMakeTable<Parameters>();
};


// The loops of the table engine, one per width. The width is in the name, so the presets of one width share the code; the table and the
// reflection are arguments. A reflected input takes the low byte, and a normal one the top byte.
constexpr uint32 CrcRunTable8(const uint8* table, uint32 start, bool reflected, std::span<const uint8> data) {
	uint32 result = start;
	if (reflected) {
		for (uint8 byte : data) {
			result = table[(result ^ byte) & 0xFFu] ^ (result >> 8);
		}
	} else {
		for (uint8 byte : data) {
			result = table[(result ^ byte) & 0xFFu];
		}
	}

	return result;
}


constexpr uint32 CrcRunTable16(const uint16* table, uint32 start, bool reflected, std::span<const uint8> data) {
	uint32 result = start;
	if (reflected) {
		for (uint8 byte : data) {
			result = table[(result ^ byte) & 0xFFu] ^ (result >> 8);
		}
	} else {
		for (uint8 byte : data) {
			result = table[((result >> 8) ^ byte) & 0xFFu] ^ ((result << 8) & 0xFFFFu);
		}
	}

	return result;
}


constexpr uint32 CrcRunTable32(const uint32* table, uint32 start, bool reflected, std::span<const uint8> data) {
	uint32 result = start;
	if (reflected) {
		for (uint8 byte : data) {
			result = table[(result ^ byte) & 0xFFu] ^ (result >> 8);
		}
	} else {
		for (uint8 byte : data) {
			result = table[((result >> 24) ^ byte) & 0xFFu] ^ (result << 8);
		}
	}

	return result;
}


// The table engine: one table lookup per byte. It is stateless; Run computes the whole CRC of the bytes it is given. Its flash cost is 256
// times the width in bytes for each algorithm used.
struct CrcTable {
	static constexpr bool synchronous = true;


	template<typename Algorithm>
	static constexpr bool Supports() {
		return true;
	}


	template<typename Algorithm>
	static constexpr uint32 Run(std::span<const uint8> data) {
		constexpr CrcParameters parameters = Algorithm::parameters;
		const uint32 start = CrcStart(parameters);
		uint32 remainder = 0;
		if constexpr (parameters.width == 8) {
			remainder = CrcRunTable8(CrcTableOf<parameters>::table.data(), start, parameters.reflectIn, data);
		} else if constexpr (parameters.width == 16) {
			remainder = CrcRunTable16(CrcTableOf<parameters>::table.data(), start, parameters.reflectIn, data);
		} else {
			remainder = CrcRunTable32(CrcTableOf<parameters>::table.data(), start, parameters.reflectIn, data);
		}

		return CrcFinish(parameters, remainder);
	}
};

}
