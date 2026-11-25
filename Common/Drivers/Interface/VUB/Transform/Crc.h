#pragma once
#include <Drivers/Interface/VUB/Meta.h>
#include <Utilities/Checksum/CRC/Crc.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/Result.h>
#include <Utilities/Result/ResultStatus.h>
#include <cstddef>
#include <span>
#include <type_traits>

namespace VUB {


// The byte order of a CRC on the wire. Low byte first is the default; high byte first is the order of some protocols. It is part of the
// wire, so it is part of the fingerprint.
enum class CrcOrder : uint8 {
	lowByteFirst,
	highByteFirst
};


// True for a Checksum::Crc type, the only type a CRC transform takes.
template<typename Type>
struct IsChecksumCrc : std::false_type {
};


template<Checksum::CrcParameters Parameters, typename Engine>
struct IsChecksumCrc<Checksum::Crc<Parameters, Engine>> : std::true_type {
};


// The mixing step of the fingerprint: the bits of value are XORed into the hash, which is multiplied by the FNV prime. Both halves of a
// 64-bit value take part.
constexpr uint32 CrcMix(uint32 hash, uint64 value) {
	const uint32 low = (hash ^ static_cast<uint32>(value)) * 16777619u;
	return (low ^ static_cast<uint32>(value >> 32)) * 16777619u;
}


// The fingerprint of the CRC parameters and the byte order: every parameter takes part, the check value does not (it is not on the wire),
// and the engine does not either.
constexpr uint32 CrcFingerprint(const Checksum::CrcParameters& parameters, CrcOrder order) {
	uint32 hash = 0x43524332u;
	hash = CrcMix(hash, parameters.width);
	hash = CrcMix(hash, parameters.polynomial);
	hash = CrcMix(hash, parameters.initialValue);
	hash = CrcMix(hash, parameters.finalXor);
	hash = CrcMix(hash, parameters.reflectIn ? 1u : 0u);
	hash = CrcMix(hash, parameters.reflectOut ? 1u : 0u);
	hash = CrcMix(hash, static_cast<uint64>(order));
	return hash;
}


// A CRC transform over a CRC type of the shared library: a Checksum::Crc, such as Checksum::Crc16Modbus, or a Checksum::Crc with another
// engine (Checksum::Crc16Modbus::With<Checksum::CrcTable>). The engine is the one of the type, so the bitwise engine is the default and a
// table is paid for only when the type names it.
//
// Encode appends the CRC of the packet, growth bytes, to the packet: growth is the width rounded up to whole bytes, so a width of 1 to 64
// bits gives 1 to 8 bytes. Order is the byte order on the wire: low byte first (the default, as Modbus) or high byte first. Decode takes
// the CRC off the end of the packet and checks it; a mismatch is crcError, so the stack counts it as a transform error. The CRC covers the
// whole packet the transform receives, so the blocks above it are covered too.
//
// The transform holds no data: the CRC type and the byte order are template arguments, so the transform is empty.
template<typename CrcType, CrcOrder Order = CrcOrder::lowByteFirst>
class Crc {
	static_assert(
		IsChecksumCrc<CrcType>::value,
		"VUB::Crc takes a CRC type of the shared CRC library: pass a Checksum::Crc, such as Checksum::Crc16Modbus, or a Checksum::Crc<Checksum::CrcParameters().Width(...)...>"
	);

	// The library's checks of the parameters and the engine run when its checked member is instantiated. Naming it here makes a transform
	// with a wrong CRC type fail to compile, even when no packet is ever encoded with it.
	static_assert(CrcType::checked, "the CRC type is not valid: see the message above");

public:
	// The CRC of a packet is growth bytes long, and the CRC parameters and the byte order are the fingerprint.
	static constexpr std::size_t growth = (CrcType::parameters.width + 7) / 8;
	static constexpr uint32 fingerprint = CrcFingerprint(CrcType::parameters, Order);


private:
	static constexpr bool highByteFirst = Order == CrcOrder::highByteFirst;
	// The CRC while its bytes are written or read: 32 bits hold a width of up to 32.
	using Word = std::conditional_t<(growth <= 4), uint32, uint64>;


public:
	// Writes the packet and its CRC: out must hold length + growth bytes.
	Result<std::size_t> Encode(const Meta&, std::span<const uint8> in, std::span<uint8> out) {
		if (out.size() < in.size() + growth) {
			return ResultStatus::invalidParameter;
		}

		for (std::size_t index = 0; index < in.size(); index++) {
			out[index] = in[index];
		}

		const Word crc = CrcType::Calculate(in);
		for (std::size_t index = 0; index < growth; index++) {
			out[in.size() + index] = static_cast<uint8>(crc >> (8 * BytePosition(index)));
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
		const Word expected = CrcType::Calculate(packet.first(body));
		Word received = 0;
		for (std::size_t index = 0; index < growth; index++) {
			received |= static_cast<Word>(packet[body + index]) << (8 * BytePosition(index));
		}

		if (received != expected) {
			return ResultStatus::crcError;
		}

		return body;
	}


private:
	// The significance of the index-th CRC byte on the wire: 0 is the least significant byte.
	static constexpr std::size_t BytePosition(std::size_t index) {
		return highByteFirst ? growth - 1 - index : index;
	}
};
}
