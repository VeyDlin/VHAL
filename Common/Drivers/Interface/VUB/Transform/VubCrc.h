#pragma once
#include <Drivers/Interface/VUB/Engine/VubCrcBitwise.h>
#include <Drivers/Interface/VUB/Engine/VubCrcCore.h>
#include <Drivers/Interface/VUB/Engine/VubCrcTable.h>
#include <Drivers/Interface/VUB/VubMeta.h>
#include <Drivers/Interface/VUB/VubRequire.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/Result.h>
#include <Utilities/Result/ResultStatus.h>
#include <cstddef>
#include <span>


// The checks of a CRC's parameters and engine, done when the transform type is instantiated. The width is 8, 16 or 32; the check value
// of the parameters is the CRC of "123456789"; the engine supports the algorithm.
template<VubCrcParameters Parameters, typename Engine>
constexpr bool VubCrcChecked() {
	VubRequire<"a CRC is 8, 16 or 32 bits wide">(Parameters.width == 8 || Parameters.width == 16 || Parameters.width == 32);
	if constexpr (Parameters.width == 8 || Parameters.width == 16 || Parameters.width == 32) {
		VubRequire<"the check value of the CRC parameters does not match the CRC of 123456789">(VubCrcCheckValue(Parameters) == Parameters.check);
		VubRequire<"the engine cannot compute this CRC: its Supports says no for these parameters">(
			Engine::template Supports<VubCrcAlgorithm<Parameters>>());
	}

	return true;
}


// A CRC transform. Encode appends the CRC of the packet, width / 8 bytes, to the packet: low byte first for Order lowByteFirst (the
// default, as Modbus), high byte first for highByteFirst. Decode takes the CRC off the end of the packet in place and checks it; a
// mismatch is crcError, so the stack counts it as a transform error. The CRC covers the whole packet the transform receives, so the
// blocks above it are covered too.
//
// Parameters is the CRC in the model of the reveng CRC catalogue. Engine computes it: VubCrcTable (the default, a 256-entry table in
// flash) or VubCrcBitwise (no table). Order is the byte order on the wire, a type parameter, so the transform holds no data: it is empty.
template<VubCrcParameters Parameters, typename Engine = VubCrcTable, VubCrcOrder Order = VubCrcOrder::lowByteFirst>
class VubCrc {
public:
	// The CRC parameters, the engine and the byte order, as the builder and the tests read them.
	static constexpr VubCrcParameters parameters = Parameters;
	static constexpr std::size_t growth = Parameters.width / 8;
	static constexpr uint32 fingerprint = VubCrcFingerprint(Parameters, Order);
	static constexpr bool checked = VubCrcChecked<Parameters, Engine>();
	static_assert(checked, "the CRC transform is not valid: see the message above");


private:
	static constexpr bool highByteFirst = Order == VubCrcOrder::highByteFirst;


public:
	// Writes the packet and its CRC: out must hold length + growth bytes.
	Result<std::size_t> Encode(const VubMeta&, std::span<const uint8> in, std::span<uint8> out) {
		if (out.size() < in.size() + growth) {
			return ResultStatus::invalidParameter;
		}

		for (std::size_t index = 0; index < in.size(); index++) {
			out[index] = in[index];
		}

		const uint32 crc = Engine::template Run<VubCrcAlgorithm<Parameters>>(in);
		for (std::size_t index = 0; index < growth; index++) {
			const std::size_t position = highByteFirst ? growth - 1 - index : index;
			out[in.size() + index] = static_cast<uint8>(crc >> (8 * position));
		}

		return in.size() + growth;
	}


	// Checks the CRC at the end of the packet and returns the length of the packet without it. The packet is changed in place only by
	// being shortened; the bytes are left as they came.
	Result<std::size_t> Decode(const VubMeta&, std::span<uint8> packet) {
		if (packet.size() < growth) {
			return ResultStatus::invalidData;
		}

		const std::size_t body = packet.size() - growth;
		const uint32 expected = Engine::template Run<VubCrcAlgorithm<Parameters>>(packet.first(body));
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
// VubCrc<Preset::parameters, VubCrcBitwise> is the same CRC with the bitwise engine, and VubCrcHighByteFirst<Preset::parameters> is
// the same CRC high byte first. Any other CRC of width 8, 16 or 32 is a VubCrc<VubCrcParameters{...}> with its check value.
using VubCrc8Smbus = VubCrc<VubCrcParameters{8, 0x07, 0x00, 0x00, false, false, 0xF4}>;
using VubCrc8Ebu = VubCrc<VubCrcParameters{8, 0x1D, 0xFF, 0x00, true, true, 0x97}>;
using VubCrc8Maxim = VubCrc<VubCrcParameters{8, 0x31, 0x00, 0x00, true, true, 0xA1}>;
using VubCrc8Wcdma = VubCrc<VubCrcParameters{8, 0x9B, 0x00, 0x00, true, true, 0x25}>;

using VubCrc16Arc = VubCrc<VubCrcParameters{16, 0x8005, 0x0000, 0x0000, true, true, 0xBB3D}>;
using VubCrc16Buypass = VubCrc<VubCrcParameters{16, 0x8005, 0x0000, 0x0000, false, false, 0xFEE8}>;
using VubCrc16CcittFalse = VubCrc<VubCrcParameters{16, 0x1021, 0xFFFF, 0x0000, false, false, 0x29B1}>;
using VubCrc16Cdma2000 = VubCrc<VubCrcParameters{16, 0xC867, 0xFFFF, 0x0000, false, false, 0x4C06}>;
using VubCrc16Cms = VubCrc<VubCrcParameters{16, 0x8005, 0xFFFF, 0x0000, false, false, 0xAEE7}>;
using VubCrc16DectR = VubCrc<VubCrcParameters{16, 0x0589, 0x0000, 0x0001, false, false, 0x007E}>;
using VubCrc16DectX = VubCrc<VubCrcParameters{16, 0x0589, 0x0000, 0x0000, false, false, 0x007F}>;
using VubCrc16Dnp = VubCrc<VubCrcParameters{16, 0x3D65, 0x0000, 0xFFFF, true, true, 0xEA82}>;
using VubCrc16Genibus = VubCrc<VubCrcParameters{16, 0x1021, 0xFFFF, 0xFFFF, false, false, 0xD64E}>;
using VubCrc16Kermit = VubCrc<VubCrcParameters{16, 0x1021, 0x0000, 0x0000, true, true, 0x2189}>;
using VubCrc16Maxim = VubCrc<VubCrcParameters{16, 0x8005, 0x0000, 0xFFFF, true, true, 0x44C2}>;
using VubCrc16Modbus = VubCrc<VubCrcParameters{16, 0x8005, 0xFFFF, 0x0000, true, true, 0x4B37}>;
using VubCrc16Riello = VubCrc<VubCrcParameters{16, 0x1021, 0xB2AA, 0x0000, true, true, 0x63D0}>;
using VubCrc16T10Dif = VubCrc<VubCrcParameters{16, 0x8BB7, 0x0000, 0x0000, false, false, 0xD0DB}>;
using VubCrc16Tms37157 = VubCrc<VubCrcParameters{16, 0x1021, 0x89EC, 0x0000, true, true, 0x26B1}>;
using VubCrc16Usb = VubCrc<VubCrcParameters{16, 0x8005, 0xFFFF, 0xFFFF, true, true, 0xB4C8}>;
using VubCrc16X25 = VubCrc<VubCrcParameters{16, 0x1021, 0xFFFF, 0xFFFF, true, true, 0x906E}>;
using VubCrc16Xmodem = VubCrc<VubCrcParameters{16, 0x1021, 0x0000, 0x0000, false, false, 0x31C3}>;

using VubCrc32IsoHdlc = VubCrc<VubCrcParameters{32, 0x04C11DB7, 0xFFFFFFFF, 0xFFFFFFFF, true, true, 0xCBF43926}>;
using VubCrc32Bzip2 = VubCrc<VubCrcParameters{32, 0x04C11DB7, 0xFFFFFFFF, 0xFFFFFFFF, false, false, 0xFC891918}>;
using VubCrc32C = VubCrc<VubCrcParameters{32, 0x1EDC6F41, 0xFFFFFFFF, 0xFFFFFFFF, true, true, 0xE3069283}>;
using VubCrc32Mpeg2 = VubCrc<VubCrcParameters{32, 0x04C11DB7, 0xFFFFFFFF, 0x00000000, false, false, 0x0376E6E7}>;
using VubCrc32Posix = VubCrc<VubCrcParameters{32, 0x04C11DB7, 0x00000000, 0xFFFFFFFF, false, false, 0x765E7680}>;
using VubCrc32Q = VubCrc<VubCrcParameters{32, 0x814141AB, 0x00000000, 0x00000000, false, false, 0x3010BF7F}>;


// The high byte first variant of a CRC: the same parameters, with the bytes of the CRC sent most significant first.
template<VubCrcParameters Parameters, typename Engine = VubCrcTable>
using VubCrcHighByteFirst = VubCrc<Parameters, Engine, VubCrcOrder::highByteFirst>;

using VubCrc16ModbusHighByteFirst = VubCrcHighByteFirst<VubCrc16Modbus::parameters>;


// Every preset is checked as soon as this header is included, not only when a chain first uses it.
static_assert(VubCrc8Smbus::checked);
static_assert(VubCrc8Ebu::checked);
static_assert(VubCrc8Maxim::checked);
static_assert(VubCrc8Wcdma::checked);
static_assert(VubCrc16Arc::checked);
static_assert(VubCrc16Buypass::checked);
static_assert(VubCrc16CcittFalse::checked);
static_assert(VubCrc16Cdma2000::checked);
static_assert(VubCrc16Cms::checked);
static_assert(VubCrc16DectR::checked);
static_assert(VubCrc16DectX::checked);
static_assert(VubCrc16Dnp::checked);
static_assert(VubCrc16Genibus::checked);
static_assert(VubCrc16Kermit::checked);
static_assert(VubCrc16Maxim::checked);
static_assert(VubCrc16Modbus::checked);
static_assert(VubCrc16ModbusHighByteFirst::checked);
static_assert(VubCrc16Riello::checked);
static_assert(VubCrc16T10Dif::checked);
static_assert(VubCrc16Tms37157::checked);
static_assert(VubCrc16Usb::checked);
static_assert(VubCrc16X25::checked);
static_assert(VubCrc16Xmodem::checked);
static_assert(VubCrc32IsoHdlc::checked);
static_assert(VubCrc32Bzip2::checked);
static_assert(VubCrc32C::checked);
static_assert(VubCrc32Mpeg2::checked);
static_assert(VubCrc32Posix::checked);
static_assert(VubCrc32Q::checked);
