#pragma once
#include <Utilities/Checksum/CRC/CrcParameters.h>
#include <Utilities/DataTypes.h>
#include <span>
#include <type_traits>

namespace Checksum {


// The bitwise engine: the byte is XORed into the register, and eight steps shift it out, so there is no table and no flash for the
// algorithm. It takes every width from 1 to 64 bits. Run takes the register in the form of CrcStart and returns the register after the
// bytes, so a call can continue another call's register.
//
// A step selects the polynomial with a mask made from the bit that leaves the register, by an arithmetic shift. The compiler makes a
// 1 KB table of a step that is written with a branch (-foptimize-crc of GCC 15); it does not recognise this form, which is also shorter.
struct CrcBitwise {
	template<CrcParameters Parameters>
	static constexpr bool Supports() {
		return true;
	}


	template<CrcParameters Parameters>
	static constexpr CrcRegister<Parameters.width> Run(CrcRegister<Parameters.width> start, std::span<const uint8> data) {
		using Register = CrcRegister<Parameters.width>;
		using Signed = std::make_signed_t<Register>;
		constexpr uint8 registerBits = sizeof(Register) * 8;
		Register remainder = start;
		for (uint8 byte : data) {
			if constexpr (Parameters.reflectIn) {
				constexpr Register polynomial = static_cast<Register>(CrcReverse(Parameters.polynomial, Parameters.width));
				remainder ^= byte;
#pragma GCC unroll 1
				for (uint8 bit = 0; bit < 8; bit++) {
					const Register mask = static_cast<Register>(static_cast<Signed>(remainder << (registerBits - 1)) >> (registerBits - 1));
					remainder = (remainder >> 1) ^ (polynomial & mask);
				}
			} else {
				constexpr Register polynomial = static_cast<Register>(Parameters.polynomial << (registerBits - Parameters.width));
				remainder ^= static_cast<Register>(byte) << (registerBits - 8);
#pragma GCC unroll 1
				for (uint8 bit = 0; bit < 8; bit++) {
					const Register mask = static_cast<Register>(static_cast<Signed>(remainder) >> (registerBits - 1));
					remainder = (remainder << 1) ^ (polynomial & mask);
				}
			}
		}

		return remainder;
	}
};
}
