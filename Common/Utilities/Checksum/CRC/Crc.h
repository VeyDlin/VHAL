#pragma once
#include <Utilities/Checksum/CRC/CrcParameters.h>
#include <Utilities/Checksum/CRC/Engine/CrcBitwise.h>
#include <Utilities/DataTypes.h>
#include <Utilities/CompileTime/Require.h>
#include <Utilities/CompileTime/TextLiteral.h>
#include <array>
#include <cstddef>
#include <span>
#include <utility>

namespace Checksum {


// The hexadecimal digit of value at position, counted from the least significant digit: '0' to '9', then 'A' to 'F'.
constexpr char CrcHexDigit(uint64 value, std::size_t position) {
	const uint64 nibble = (value >> (4 * position)) & 0xF;
	return nibble < 10 ? static_cast<char>('0' + nibble) : static_cast<char>('A' + nibble - 10);
}


// Declared, never defined: a failed check of a CRC's check value. The message is the first template argument; the digits of the value the
// parameters give follow, most significant first, so the diagnostic shows that value in hexadecimal. One call carries both, because a
// constant evaluation stops at the first failed call.
template<TextLiteral Message, char... Digits>
void CrcCheckFailed();


// Fails the check with the hexadecimal digits of Value, one per position of the index sequence.
template<uint64 Value, std::size_t... Position>
constexpr void CrcShowCheckValue(std::index_sequence<Position...>) {
	CrcCheckFailed<"CRC: the check value of the CRC parameters does not match the CRC of 123456789",
		CrcHexDigit(Value, sizeof...(Position) - 1 - Position)...>();
}


// The text "123456789" that every check value is the CRC of.
inline constexpr std::array<uint8, 9> CrcCheckText = {0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39};


// The check value of the parameters: the CRC of CrcCheckText, from the bitwise engine, so no table is involved.
template<CrcParameters Parameters>
constexpr uint64 CrcCheckValueOf() {
	return CrcFinish<Parameters>(CrcBitwise::Run<Parameters>(CrcStart<Parameters>(), std::span<const uint8>(CrcCheckText)));
}


// The checks of a CRC's parameters and engine, made when the Crc type is instantiated. The width and the polynomial are required, the
// width is 1 to 64 bits, the polynomial, the initial value and the final XOR fit the width, the check value (when given) is the CRC of
// "123456789", and the engine supports the algorithm. Each failure is a compile error with its message.
template<CrcParameters Parameters, typename Engine>
constexpr bool CrcChecked() {
	Require<"CRC: the CRC parameters have no Width: chain Width(...) with the width in bits, 1 to 64">(Parameters.hasWidth);
	Require<"CRC: the CRC parameters have no Polynomial: chain Polynomial(...) with the polynomial in normal form">(Parameters.hasPolynomial);
	if constexpr (Parameters.hasWidth && Parameters.hasPolynomial) {
		Require<"CRC: a CRC is 1 to 64 bits wide: the Width(...) of the CRC parameters is outside that range">(
			Parameters.width >= 1 && Parameters.width <= 64);
		if constexpr (Parameters.width >= 1 && Parameters.width <= 64) {
			Require<"CRC: the polynomial of the CRC parameters is wider than the width">(
				CrcFits(Parameters.polynomial, Parameters.width));
			Require<"CRC: the initial value of the CRC parameters is wider than the width">(
				CrcFits(Parameters.initialValue, Parameters.width));
			Require<"CRC: the final XOR of the CRC parameters is wider than the width">(
				CrcFits(Parameters.finalXor, Parameters.width));
			if constexpr (Parameters.hasCheck) {
				if constexpr (CrcCheckValueOf<Parameters>() != Parameters.check) {
					CrcShowCheckValue<CrcCheckValueOf<Parameters>()>(std::make_index_sequence<(Parameters.width + 3) / 4>());
				}
			}

			Require<"CRC: the engine cannot compute this CRC: its Supports says no for these parameters">(
				Engine::template Supports<Parameters>());
		}
	}

	return true;
}


// A CRC of the parameters, computed by an engine. The object holds the register between calls, so the data can come in parts: a
// message split at any point gives the same CRC. Calculate is the one-call form. The type is the parameters and the engine: the
// default engine is CrcBitwise, and With<Engine> names the same CRC with another one.
template<CrcParameters Parameters, typename Engine = CrcBitwise>
class Crc {
public:
	using Value = CrcValue<Parameters.width>;

	static constexpr CrcParameters parameters = Parameters;
	static constexpr bool checked = CrcChecked<Parameters, Engine>();

	template<typename OtherEngine>
	using With = Crc<Parameters, OtherEngine>;


	constexpr Crc() : remainder(Start()) {}


	static constexpr Value Calculate(std::span<const uint8> data) {
		Crc crc;
		crc.Add(data);
		return crc.Get();
	}


	constexpr void Add(std::span<const uint8> data) {
		remainder = Engine::template Run<Parameters>(remainder, data);
	}


	constexpr Value Get() const {
		return static_cast<Value>(CrcFinish<Parameters>(remainder));
	}


	constexpr void Reset() {
		remainder = Start();
	}

private:
	using Register = CrcRegister<Parameters.width>;


	Register remainder;


	// Every object starts here, so the checks run at the first use of the type.
	static constexpr Register Start() {
		static_assert(checked, "the CRC parameters or the engine are not valid: see the message above");
		return CrcStart<Parameters>();
	}
};
}
