#pragma once
#include <Utilities/DataTypes.h>
#include <array>
#include <cstddef>
#include <span>

namespace VUB {


// The parameters of a CRC in the model of the reveng CRC catalogue: the width, the polynomial, the initial value (the value of the
// unreflected register, also for a reflected input), the final XOR, the reflection of the input and of the output, and the check value
// of the catalogue, which is the CRC of the ASCII string "123456789". The width is 8, 16 or 32 bits; the transform refuses other widths.
struct CrcParameters {
	uint8 width;
	uint32 polynomial;
	uint32 initialValue;
	uint32 finalXor;
	bool reflectIn;
	bool reflectOut;
	uint32 check;
};


// The byte order of a CRC on the wire. Low byte first is the default; high byte first is the order of some protocols, and it is part of
// the wire, so it is part of the fingerprint.
enum class CrcOrder : uint8 {
	lowByteFirst,
	highByteFirst
};


// The algorithm an engine is given. The parameters are a type, so an engine's Supports and Run take one template argument.
template<CrcParameters Parameters>
struct CrcAlgorithm {
	static constexpr CrcParameters parameters = Parameters;
};


// The bits of a width: the low width bits set.
constexpr uint32 CrcMask(uint8 width) {
	return width >= 32 ? 0xFFFFFFFFu : ((1u << width) - 1u);
}


// The low width bits of value, in reverse order.
constexpr uint32 CrcReverse(uint32 value, uint8 width) {
	uint32 result = 0;
	for (uint8 bit = 0; bit < width; bit++) {
		result = (result << 1) | ((value >> bit) & 1u);
	}

	return result;
}


// The register before the first byte. A reflected input keeps its register reflected, so the initial value is reflected too.
constexpr uint32 CrcStart(const CrcParameters& parameters) {
	const uint32 initial = parameters.initialValue & CrcMask(parameters.width);
	return parameters.reflectIn ? CrcReverse(initial, parameters.width) : initial;
}


// The CRC from the register after the last byte. The register is reflected when the input and the output reflection differ, and the
// final XOR is applied last.
constexpr uint32 CrcFinish(const CrcParameters& parameters, uint32 remainder) {
	uint32 result = remainder;
	if (parameters.reflectIn != parameters.reflectOut) {
		result = CrcReverse(result, parameters.width);
	}

	return (result ^ parameters.finalXor) & CrcMask(parameters.width);
}


// The CRC of the bytes, one bit at a time, with no table. The polynomial is in normal form; a reflected input uses it reversed. Start is
// the register from CrcStart, and the result is the register before CrcFinish. This loop is shared by every width and preset.
constexpr uint32 CrcBitRun(uint8 width, uint32 polynomial, bool reflected, uint32 start, std::span<const uint8> data) {
	const uint32 mask = CrcMask(width);
	uint32 result = start;
	if (reflected) {
		const uint32 reversed = CrcReverse(polynomial, width);
		for (uint8 byte : data) {
			result ^= static_cast<uint32>(byte);
			for (uint8 bit = 0; bit < 8; bit++) {
				result = (result & 1u) != 0 ? (result >> 1) ^ reversed : result >> 1;
			}
		}
	} else {
		const uint32 top = 1u << (width - 1);
		for (uint8 byte : data) {
			result ^= static_cast<uint32>(byte) << (width - 8);
			for (uint8 bit = 0; bit < 8; bit++) {
				result = (result & top) != 0 ? ((result << 1) ^ polynomial) & mask : (result << 1) & mask;
			}
		}
	}

	return result;
}


// The check value of the parameters: the CRC of "123456789". Computed at compile time from the bitwise loop, so no engine is needed.
constexpr uint32 CrcCheckValue(const CrcParameters& parameters) {
	constexpr std::array<uint8, 9> digits = {0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39};
	std::span<const uint8> data(digits.data(), digits.size());
	const uint32 remainder = CrcBitRun(parameters.width, parameters.polynomial, parameters.reflectIn, CrcStart(parameters), data);
	return CrcFinish(parameters, remainder);
}


// The fingerprint of the parameters and the byte order: the same values give the same value, and every field takes part. The engine
// does not: the wire does not depend on it.
constexpr uint32 CrcMix(uint32 hash, uint32 value) {
	return (hash ^ value) * 16777619u;
}


constexpr uint32 CrcFingerprint(const CrcParameters& parameters, CrcOrder order) {
	uint32 hash = 0x43524331u;
	hash = CrcMix(hash, parameters.width);
	hash = CrcMix(hash, parameters.polynomial);
	hash = CrcMix(hash, parameters.initialValue);
	hash = CrcMix(hash, parameters.finalXor);
	hash = CrcMix(hash, parameters.reflectIn ? 1u : 0u);
	hash = CrcMix(hash, parameters.reflectOut ? 1u : 0u);
	hash = CrcMix(hash, static_cast<uint32>(order));
	return hash;
}

}
