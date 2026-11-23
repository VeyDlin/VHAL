#pragma once
#include <Drivers/Interface/VUB/Engine/CrcBitwise.h>
#include <Drivers/Interface/VUB/Engine/CrcCore.h>
#include <Drivers/Interface/VUB/Engine/CrcTable.h>
#include <Drivers/Interface/VUB/Meta.h>
#include <Drivers/Interface/VUB/Require.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/Result.h>
#include <Utilities/Result/ResultStatus.h>
#include <cstddef>
#include <span>
#include <utility>

namespace VUB {


// The hexadecimal digit of value at position, counted from the least significant digit: '0' to '9', then 'A' to 'F'.
constexpr char CrcHexDigit(uint32 value, std::size_t position) {
	const uint32 nibble = (value >> (4 * position)) & 0xFu;
	return nibble < 10 ? static_cast<char>('0' + nibble) : static_cast<char>('A' + nibble - 10);
}


// Declared, never defined: a failed check of a CRC's check value. The message is the first template argument; the digits of the value the
// parameters give follow, most significant first, so the diagnostic shows that value in hexadecimal. One call carries both, because a
// constant evaluation stops at the first failed call.
template<TextLiteral Message, char... Digits>
void CrcCheckFailed();


// Fails the check with the hexadecimal digits of Value, one per position of the index sequence.
template<uint32 Value, std::size_t... Position>
constexpr void CrcShowCheckValue(std::index_sequence<Position...>) {
	CrcCheckFailed<"the check value of the CRC parameters does not match the CRC of 123456789",
		CrcHexDigit(Value, sizeof...(Position) - 1 - Position)...>();
}


// The checks of a CRC's parameters and engine, done when the transform type is instantiated. The width and the polynomial are required,
// and the width is 8, 16 or 32. The check value is optional: when it is given, it must be the CRC of "123456789", and a wrong one shows the
// value the parameters give. The engine supports the algorithm.
template<CrcParameters Parameters, typename Engine>
constexpr bool CrcChecked() {
	Require<"the CRC parameters have no Width: chain Width(8), Width(16) or Width(32)">(Parameters.hasWidth);
	Require<"the CRC parameters have no Polynomial: chain Polynomial(...) with the polynomial in normal form">(Parameters.hasPolynomial);
	if constexpr (Parameters.hasWidth && Parameters.hasPolynomial) {
		Require<"a CRC is 8, 16 or 32 bits wide">(Parameters.width == 8 || Parameters.width == 16 || Parameters.width == 32);
		if constexpr (Parameters.width == 8 || Parameters.width == 16 || Parameters.width == 32) {
			if constexpr (Parameters.hasCheck && CrcCheckValue(Parameters) != Parameters.check) {
				CrcShowCheckValue<CrcCheckValue(Parameters)>(std::make_index_sequence<Parameters.width / 4>());
			}

			Require<"the engine cannot compute this CRC: its Supports says no for these parameters">(
				Engine::template Supports<CrcAlgorithm<Parameters>>());
		}
	}

	return true;
}


// A CRC transform. Encode appends the CRC of the packet, width / 8 bytes, to the packet: low byte first for Order lowByteFirst (the
// default, as Modbus), high byte first for highByteFirst. Decode takes the CRC off the end of the packet in place and checks it; a
// mismatch is crcError, so the stack counts it as a transform error. The CRC covers the whole packet the transform receives, so the
// blocks above it are covered too.
//
// Parameters is the CRC in the model of the reveng CRC catalogue. Engine computes it: CrcTable (the default, a 256-entry table in
// flash) or CrcBitwise (no table). Order is the byte order on the wire, a type parameter, so the transform holds no data: it is empty.
template<CrcParameters Parameters, typename Engine = CrcTable, CrcOrder Order = CrcOrder::lowByteFirst>
class Crc {
public:
	// The CRC parameters, the engine and the byte order, as the builder and the tests read them.
	static constexpr CrcParameters parameters = Parameters;
	static constexpr std::size_t growth = Parameters.width / 8;
	static constexpr uint32 fingerprint = CrcFingerprint(Parameters, Order);
	static constexpr bool checked = CrcChecked<Parameters, Engine>();
	static_assert(checked, "the CRC transform is not valid: see the message above");


private:
	static constexpr bool highByteFirst = Order == CrcOrder::highByteFirst;


public:
	// Writes the packet and its CRC: out must hold length + growth bytes.
	Result<std::size_t> Encode(const Meta&, std::span<const uint8> in, std::span<uint8> out) {
		if (out.size() < in.size() + growth) {
			return ResultStatus::invalidParameter;
		}

		for (std::size_t index = 0; index < in.size(); index++) {
			out[index] = in[index];
		}

		const uint32 crc = Engine::template Run<CrcAlgorithm<Parameters>>(in);
		for (std::size_t index = 0; index < growth; index++) {
			const std::size_t position = highByteFirst ? growth - 1 - index : index;
			out[in.size() + index] = static_cast<uint8>(crc >> (8 * position));
		}

		return in.size() + growth;
	}


	// Checks the CRC at the end of the packet and returns the length of the packet without it. The packet is changed in place only by
	// being shortened; the bytes are left as they came.
	Result<std::size_t> Decode(const Meta&, std::span<uint8> packet) {
		if (packet.size() < growth) {
			return ResultStatus::invalidData;
		}

		const std::size_t body = packet.size() - growth;
		const uint32 expected = Engine::template Run<CrcAlgorithm<Parameters>>(packet.first(body));
		uint32 received = 0;
		for (std::size_t index = 0; index < growth; index++) {
			const std::size_t position = highByteFirst ? growth - 1 - index : index;
			received |= static_cast<uint32>(packet[body + index]) << (8 * position);
		}

		if (received != expected) {
			return ResultStatus::crcError;
		}

		return body;
	}
};


// The presets: the CRCs of the shared CRC library (Utilities/Checksum/CRC/Crc.h) of width 8, 16 or 32, and CRC-16/RIELLO and
// CRC-16/TMS37157, named as the reveng catalogue names them. Each is a transform type, with the table engine and low byte first;
// Crc<Preset::parameters, CrcBitwise> is the same CRC with the bitwise engine, and CrcHighByteFirst<Preset::parameters> is
// the same CRC high byte first. Each is written as a chain with its check value; a reflection that is off is left out, as it is the
// default. Any other CRC of width 8, 16 or 32 is a Crc<CrcParameters().Width(...)...> with its check value.
using Crc8Smbus = Crc<CrcParameters().Width(8).Polynomial(0x07).Initial(0x00).FinalXor(0x00).Check(0xF4)>;
using Crc8Ebu = Crc<CrcParameters().Width(8).Polynomial(0x1D).Initial(0xFF).FinalXor(0x00).ReflectIn().ReflectOut().Check(0x97)>;
using Crc8Maxim = Crc<CrcParameters().Width(8).Polynomial(0x31).Initial(0x00).FinalXor(0x00).ReflectIn().ReflectOut().Check(0xA1)>;
using Crc8Wcdma = Crc<CrcParameters().Width(8).Polynomial(0x9B).Initial(0x00).FinalXor(0x00).ReflectIn().ReflectOut().Check(0x25)>;

using Crc16Arc = Crc<CrcParameters().Width(16).Polynomial(0x8005).Initial(0x0000).FinalXor(0x0000).ReflectIn().ReflectOut().Check(0xBB3D)>;
using Crc16Buypass = Crc<CrcParameters().Width(16).Polynomial(0x8005).Initial(0x0000).FinalXor(0x0000).Check(0xFEE8)>;
using Crc16CcittFalse = Crc<CrcParameters().Width(16).Polynomial(0x1021).Initial(0xFFFF).FinalXor(0x0000).Check(0x29B1)>;
using Crc16Cdma2000 = Crc<CrcParameters().Width(16).Polynomial(0xC867).Initial(0xFFFF).FinalXor(0x0000).Check(0x4C06)>;
using Crc16Cms = Crc<CrcParameters().Width(16).Polynomial(0x8005).Initial(0xFFFF).FinalXor(0x0000).Check(0xAEE7)>;
using Crc16DectR = Crc<CrcParameters().Width(16).Polynomial(0x0589).Initial(0x0000).FinalXor(0x0001).Check(0x007E)>;
using Crc16DectX = Crc<CrcParameters().Width(16).Polynomial(0x0589).Initial(0x0000).FinalXor(0x0000).Check(0x007F)>;
using Crc16Dnp = Crc<CrcParameters().Width(16).Polynomial(0x3D65).Initial(0x0000).FinalXor(0xFFFF).ReflectIn().ReflectOut().Check(0xEA82)>;
using Crc16Genibus = Crc<CrcParameters().Width(16).Polynomial(0x1021).Initial(0xFFFF).FinalXor(0xFFFF).Check(0xD64E)>;
using Crc16Kermit = Crc<CrcParameters().Width(16).Polynomial(0x1021).Initial(0x0000).FinalXor(0x0000).ReflectIn().ReflectOut().Check(0x2189)>;
using Crc16Maxim = Crc<CrcParameters().Width(16).Polynomial(0x8005).Initial(0x0000).FinalXor(0xFFFF).ReflectIn().ReflectOut().Check(0x44C2)>;
using Crc16Modbus = Crc<CrcParameters().Width(16).Polynomial(0x8005).Initial(0xFFFF).FinalXor(0x0000).ReflectIn().ReflectOut().Check(0x4B37)>;
using Crc16Riello = Crc<CrcParameters().Width(16).Polynomial(0x1021).Initial(0xB2AA).FinalXor(0x0000).ReflectIn().ReflectOut().Check(0x63D0)>;
using Crc16T10Dif = Crc<CrcParameters().Width(16).Polynomial(0x8BB7).Initial(0x0000).FinalXor(0x0000).Check(0xD0DB)>;
using Crc16Tms37157 = Crc<CrcParameters().Width(16).Polynomial(0x1021).Initial(0x89EC).FinalXor(0x0000).ReflectIn().ReflectOut().Check(0x26B1)>;
using Crc16Usb = Crc<CrcParameters().Width(16).Polynomial(0x8005).Initial(0xFFFF).FinalXor(0xFFFF).ReflectIn().ReflectOut().Check(0xB4C8)>;
using Crc16X25 = Crc<CrcParameters().Width(16).Polynomial(0x1021).Initial(0xFFFF).FinalXor(0xFFFF).ReflectIn().ReflectOut().Check(0x906E)>;
using Crc16Xmodem = Crc<CrcParameters().Width(16).Polynomial(0x1021).Initial(0x0000).FinalXor(0x0000).Check(0x31C3)>;

using Crc32IsoHdlc = Crc<CrcParameters().Width(32).Polynomial(0x04C11DB7).Initial(0xFFFFFFFF).FinalXor(0xFFFFFFFF).ReflectIn().ReflectOut().Check(0xCBF43926)>;
using Crc32Bzip2 = Crc<CrcParameters().Width(32).Polynomial(0x04C11DB7).Initial(0xFFFFFFFF).FinalXor(0xFFFFFFFF).Check(0xFC891918)>;
using Crc32C = Crc<CrcParameters().Width(32).Polynomial(0x1EDC6F41).Initial(0xFFFFFFFF).FinalXor(0xFFFFFFFF).ReflectIn().ReflectOut().Check(0xE3069283)>;
using Crc32Mpeg2 = Crc<CrcParameters().Width(32).Polynomial(0x04C11DB7).Initial(0xFFFFFFFF).FinalXor(0x00000000).Check(0x0376E6E7)>;
using Crc32Posix = Crc<CrcParameters().Width(32).Polynomial(0x04C11DB7).Initial(0x00000000).FinalXor(0xFFFFFFFF).Check(0x765E7680)>;
using Crc32Q = Crc<CrcParameters().Width(32).Polynomial(0x814141AB).Initial(0x00000000).FinalXor(0x00000000).Check(0x3010BF7F)>;


// The high byte first variant of a CRC: the same parameters, with the bytes of the CRC sent most significant first.
template<CrcParameters Parameters, typename Engine = CrcTable>
using CrcHighByteFirst = Crc<Parameters, Engine, CrcOrder::highByteFirst>;

using Crc16ModbusHighByteFirst = CrcHighByteFirst<Crc16Modbus::parameters>;


// Every preset is checked as soon as this header is included, not only when a chain first uses it.
static_assert(Crc8Smbus::checked);
static_assert(Crc8Ebu::checked);
static_assert(Crc8Maxim::checked);
static_assert(Crc8Wcdma::checked);
static_assert(Crc16Arc::checked);
static_assert(Crc16Buypass::checked);
static_assert(Crc16CcittFalse::checked);
static_assert(Crc16Cdma2000::checked);
static_assert(Crc16Cms::checked);
static_assert(Crc16DectR::checked);
static_assert(Crc16DectX::checked);
static_assert(Crc16Dnp::checked);
static_assert(Crc16Genibus::checked);
static_assert(Crc16Kermit::checked);
static_assert(Crc16Maxim::checked);
static_assert(Crc16Modbus::checked);
static_assert(Crc16ModbusHighByteFirst::checked);
static_assert(Crc16Riello::checked);
static_assert(Crc16T10Dif::checked);
static_assert(Crc16Tms37157::checked);
static_assert(Crc16Usb::checked);
static_assert(Crc16X25::checked);
static_assert(Crc16Xmodem::checked);
static_assert(Crc32IsoHdlc::checked);
static_assert(Crc32Bzip2::checked);
static_assert(Crc32C::checked);
static_assert(Crc32Mpeg2::checked);
static_assert(Crc32Posix::checked);
static_assert(Crc32Q::checked);

}
