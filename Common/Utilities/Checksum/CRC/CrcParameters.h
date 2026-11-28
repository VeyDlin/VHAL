#pragma once
#include <Utilities/DataTypes.h>
#include <type_traits>

namespace Checksum {


// The parameters of a CRC in the model of the reveng CRC catalogue: the width, the polynomial in normal form (without the leading
// term), the initial value of the unreflected register, the final XOR, the reflection of the input and of the output, and the check
// value, which is the CRC of the ASCII string "123456789". The widths are 1 to 64 bits. The parameters are written as a chain:
// CrcParameters().Width(16).Polynomial(0x8005).Initial(0xFFFF).ReflectIn().ReflectOut().Check(0x4B37). Width and Polynomial are
// required; Initial, FinalXor and the reflections default to 0 and false; Check is optional, and without it no check runs.
struct CrcParameters {
	uint8 width = 0;
	bool hasWidth = false;
	uint64 polynomial = 0;
	bool hasPolynomial = false;
	uint64 initialValue = 0;
	uint64 finalXor = 0;
	bool reflectIn = false;
	bool reflectOut = false;
	uint64 check = 0;
	bool hasCheck = false;


	// A default-constructed set has no width, no polynomial and no check. The declared constructor makes the struct not an aggregate:
	// a brace list does not give the parameters, only the chain does.
	constexpr CrcParameters() = default;


	// Each method of the chain gives a copy with one field set. A width above 255 is kept as 255, so the width check refuses it.
	constexpr CrcParameters Width(uint32 value) const {
		CrcParameters result = *this;
		result.width = static_cast<uint8>(value > 255 ? 255 : value);
		result.hasWidth = true;
		return result;
	}


	constexpr CrcParameters Polynomial(uint64 value) const {
		CrcParameters result = *this;
		result.polynomial = value;
		result.hasPolynomial = true;
		return result;
	}


	constexpr CrcParameters Initial(uint64 value) const {
		CrcParameters result = *this;
		result.initialValue = value;
		return result;
	}


	constexpr CrcParameters FinalXor(uint64 value) const {
		CrcParameters result = *this;
		result.finalXor = value;
		return result;
	}


	constexpr CrcParameters ReflectIn(bool value = true) const {
		CrcParameters result = *this;
		result.reflectIn = value;
		return result;
	}


	constexpr CrcParameters ReflectOut(bool value = true) const {
		CrcParameters result = *this;
		result.reflectOut = value;
		return result;
	}


	constexpr CrcParameters Check(uint64 value) const {
		CrcParameters result = *this;
		result.check = value;
		result.hasCheck = true;
		return result;
	}
};


// The register of a CRC of width bits: uint32 up to 32 bits, uint64 above.
template<uint8 Width>
using CrcRegister = std::conditional_t<(Width <= 32), uint32, uint64>;


// The value of a CRC of width bits, and the type of a table entry: the smallest unsigned type that holds the width.
template<uint8 Width>
using CrcValue = std::conditional_t<(Width <= 8), uint8, std::conditional_t<(Width <= 16), uint16, std::conditional_t<(Width <= 32), uint32, uint64>>>;


// The low width bits set. Width 64 gives all bits.
constexpr uint64 CrcMask(uint8 width) {
	return width >= 64 ? ~static_cast<uint64>(0) : (static_cast<uint64>(1) << width) - 1;
}


// True when value has no bit at or above width.
constexpr bool CrcFits(uint64 value, uint8 width) {
	return width >= 64 || (value >> width) == 0;
}


// The low width bits of value, in reverse order.
constexpr uint64 CrcReverse(uint64 value, uint8 width) {
	uint64 result = 0;
	for (uint8 bit = 0; bit < width; bit++) {
		result = (result << 1) | ((value >> bit) & 1);
	}

	return result;
}


// The register at the start of a message, in the form of the engine contract. A reflected input keeps the register reflected, so the
// initial value is reversed. A normal input keeps the register top-aligned: the CRC occupies the top width bits of the register type,
// and the bits below are zero.
template<CrcParameters Parameters>
constexpr CrcRegister<Parameters.width> CrcStart() {
	using Register = CrcRegister<Parameters.width>;
	constexpr uint8 registerBits = sizeof(Register) * 8;
	const uint64 initial = Parameters.initialValue & CrcMask(Parameters.width);
	if constexpr (Parameters.reflectIn) {
		return static_cast<Register>(CrcReverse(initial, Parameters.width));
	} else {
		return static_cast<Register>(initial << (registerBits - Parameters.width));
	}
}


// The CRC from the register after the last byte: the register is brought to the output's reflection, and the final XOR is applied last.
template<CrcParameters Parameters>
constexpr uint64 CrcFinish(CrcRegister<Parameters.width> remainder) {
	constexpr uint8 registerBits = sizeof(CrcRegister<Parameters.width>) * 8;
	uint64 value = 0;
	if constexpr (Parameters.reflectIn) {
		value = Parameters.reflectOut ? static_cast<uint64>(remainder) : CrcReverse(remainder, Parameters.width);
	} else {
		value = static_cast<uint64>(remainder) >> (registerBits - Parameters.width);
		if constexpr (Parameters.reflectOut) {
			value = CrcReverse(value, Parameters.width);
		}
	}

	return (value ^ Parameters.finalXor) & CrcMask(Parameters.width);
}


// The register whose CrcFinish is the finished value: the inverse of CrcFinish. The finished value is the CRC of the head of a
// message, so the register continues that message. The register is in the form CrcStart gives, which is the form every engine keeps.
template<CrcParameters Parameters>
constexpr CrcRegister<Parameters.width> CrcResume(uint64 finished) {
	using Register = CrcRegister<Parameters.width>;
	constexpr uint8 registerBits = sizeof(Register) * 8;
	const uint64 value = (finished ^ Parameters.finalXor) & CrcMask(Parameters.width);
	if constexpr (Parameters.reflectIn) {
		return static_cast<Register>(Parameters.reflectOut ? value : CrcReverse(value, Parameters.width));
	} else {
		const uint64 aligned = Parameters.reflectOut ? CrcReverse(value, Parameters.width) : value;
		return static_cast<Register>(aligned << (registerBits - Parameters.width));
	}
}
}
