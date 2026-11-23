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

namespace VUB {


// The checks of a CRC's parameters and engine, done when the transform type is instantiated. The width is 8, 16 or 32; the check value
// of the parameters is the CRC of "123456789"; the engine supports the algorithm.
template<CrcParameters Parameters, typename Engine>
constexpr bool CrcChecked() {
	Require<"a CRC is 8, 16 or 32 bits wide">(Parameters.width == 8 || Parameters.width == 16 || Parameters.width == 32);
	if constexpr (Parameters.width == 8 || Parameters.width == 16 || Parameters.width == 32) {
		Require<"the check value of the CRC parameters does not match the CRC of 123456789">(CrcCheckValue(Parameters) == Parameters.check);
		Require<"the engine cannot compute this CRC: its Supports says no for these parameters">(
			Engine::template Supports<CrcAlgorithm<Parameters>>());
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
// the same CRC high byte first. Any other CRC of width 8, 16 or 32 is a Crc<CrcParameters{...}> with its check value.
using Crc8Smbus = Crc<CrcParameters{8, 0x07, 0x00, 0x00, false, false, 0xF4}>;
using Crc8Ebu = Crc<CrcParameters{8, 0x1D, 0xFF, 0x00, true, true, 0x97}>;
using Crc8Maxim = Crc<CrcParameters{8, 0x31, 0x00, 0x00, true, true, 0xA1}>;
using Crc8Wcdma = Crc<CrcParameters{8, 0x9B, 0x00, 0x00, true, true, 0x25}>;

using Crc16Arc = Crc<CrcParameters{16, 0x8005, 0x0000, 0x0000, true, true, 0xBB3D}>;
using Crc16Buypass = Crc<CrcParameters{16, 0x8005, 0x0000, 0x0000, false, false, 0xFEE8}>;
using Crc16CcittFalse = Crc<CrcParameters{16, 0x1021, 0xFFFF, 0x0000, false, false, 0x29B1}>;
using Crc16Cdma2000 = Crc<CrcParameters{16, 0xC867, 0xFFFF, 0x0000, false, false, 0x4C06}>;
using Crc16Cms = Crc<CrcParameters{16, 0x8005, 0xFFFF, 0x0000, false, false, 0xAEE7}>;
using Crc16DectR = Crc<CrcParameters{16, 0x0589, 0x0000, 0x0001, false, false, 0x007E}>;
using Crc16DectX = Crc<CrcParameters{16, 0x0589, 0x0000, 0x0000, false, false, 0x007F}>;
using Crc16Dnp = Crc<CrcParameters{16, 0x3D65, 0x0000, 0xFFFF, true, true, 0xEA82}>;
using Crc16Genibus = Crc<CrcParameters{16, 0x1021, 0xFFFF, 0xFFFF, false, false, 0xD64E}>;
using Crc16Kermit = Crc<CrcParameters{16, 0x1021, 0x0000, 0x0000, true, true, 0x2189}>;
using Crc16Maxim = Crc<CrcParameters{16, 0x8005, 0x0000, 0xFFFF, true, true, 0x44C2}>;
using Crc16Modbus = Crc<CrcParameters{16, 0x8005, 0xFFFF, 0x0000, true, true, 0x4B37}>;
using Crc16Riello = Crc<CrcParameters{16, 0x1021, 0xB2AA, 0x0000, true, true, 0x63D0}>;
using Crc16T10Dif = Crc<CrcParameters{16, 0x8BB7, 0x0000, 0x0000, false, false, 0xD0DB}>;
using Crc16Tms37157 = Crc<CrcParameters{16, 0x1021, 0x89EC, 0x0000, true, true, 0x26B1}>;
using Crc16Usb = Crc<CrcParameters{16, 0x8005, 0xFFFF, 0xFFFF, true, true, 0xB4C8}>;
using Crc16X25 = Crc<CrcParameters{16, 0x1021, 0xFFFF, 0xFFFF, true, true, 0x906E}>;
using Crc16Xmodem = Crc<CrcParameters{16, 0x1021, 0x0000, 0x0000, false, false, 0x31C3}>;

using Crc32IsoHdlc = Crc<CrcParameters{32, 0x04C11DB7, 0xFFFFFFFF, 0xFFFFFFFF, true, true, 0xCBF43926}>;
using Crc32Bzip2 = Crc<CrcParameters{32, 0x04C11DB7, 0xFFFFFFFF, 0xFFFFFFFF, false, false, 0xFC891918}>;
using Crc32C = Crc<CrcParameters{32, 0x1EDC6F41, 0xFFFFFFFF, 0xFFFFFFFF, true, true, 0xE3069283}>;
using Crc32Mpeg2 = Crc<CrcParameters{32, 0x04C11DB7, 0xFFFFFFFF, 0x00000000, false, false, 0x0376E6E7}>;
using Crc32Posix = Crc<CrcParameters{32, 0x04C11DB7, 0x00000000, 0xFFFFFFFF, false, false, 0x765E7680}>;
using Crc32Q = Crc<CrcParameters{32, 0x814141AB, 0x00000000, 0x00000000, false, false, 0x3010BF7F}>;


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
