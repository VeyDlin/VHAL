#pragma once
#include <DemoUSBNetwork/Network/InternetChecksum.h>
#include <DemoUSBNetwork/Network/NetworkBytes.h>
#include <Utilities/DataTypes.h>
#include <cstddef>
#include <span>


// IPv4 (RFC 791) without options on the way out, and the header with options on the way in. Fragments are not reassembled: a packet
// with the more-fragments flag or a fragment offset is refused, so the answer never depends on another packet.
namespace DemoNetwork {


constexpr std::size_t Ipv4MinHeaderSize = 20;
constexpr uint8 IpProtocolIcmp = 1;
constexpr uint8 IpProtocolUdp = 17;
constexpr uint8 Ipv4DefaultTtl = 64;
constexpr uint32 BroadcastAddress = 0xFFFFFFFF;


// A packet that passed ParseIpv4(). payload is the bytes after the header, up to the packet's total length.
struct Ipv4Packet {
	uint8 protocol = 0;
	uint32 source = 0;
	uint32 destination = 0;
	std::span<const uint8> payload{};
};


// True for an address that may be the source of an answer: not 0.0.0.0, not the broadcast address and not a multicast group (224/4).
inline bool IsUnicastAddress(uint32 address) {
	return address != 0 && address != BroadcastAddress && (address >> 28) != 0x0E;
}


// Checks the IPv4 packet at the start of packet (the Ethernet payload). The version is 4, the header length (IHL) is 5 to 15 words
// and lies inside the packet, and the total length covers the header and lies inside the packet. Bytes after the total length are
// Ethernet padding and are ignored. The header checksum must be right. A fragment is refused. False for any of these.
inline bool ParseIpv4(std::span<const uint8> packet, Ipv4Packet& result) {
	if (packet.size() < Ipv4MinHeaderSize) {
		return false;
	}

	uint8 versionAndLength = packet[0];
	if ((versionAndLength >> 4) != 4) {
		return false;
	}

	std::size_t headerSize = static_cast<std::size_t>(versionAndLength & 0x0F) * 4;
	if (headerSize < Ipv4MinHeaderSize || headerSize > packet.size()) {
		return false;
	}

	std::size_t totalLength = ReadBe16(packet, 2);
	if (totalLength < headerSize || totalLength > packet.size()) {
		return false;
	}

	// The flags (0x2000, more fragments) and the fragment offset (0x1FFF) are the low 14 bits; the DF flag (0x4000) is allowed.
	if ((ReadBe16(packet, 6) & 0x3FFF) != 0) {
		return false;
	}

	if (OnesComplementSum(packet.first(headerSize), 0) != 0xFFFF) {
		return false;
	}

	result.protocol = packet[9];
	result.source = ReadBe32(packet, 12);
	result.destination = ReadBe32(packet, 16);
	result.payload = packet.subspan(headerSize, totalLength - headerSize);
	return true;
}


// The sum of the UDP pseudo header (RFC 768): source, destination, protocol and the UDP length. It is the initial value of the UDP checksum.
inline uint32 PseudoHeaderSum(uint32 source, uint32 destination, uint8 protocol, uint16 length) {
	return (source >> 16) + (source & 0xFFFF) + (destination >> 16) + (destination & 0xFFFF) + protocol + length;
}


// Writes a 20-byte IPv4 header without options at the start of packet, with DF set, TTL 64 and the checksum filled in. totalLength
// counts this header and the payload that follows it. The identification is 0, as every packet of the device is a single fragment.
inline void WriteIpv4Header(std::span<uint8> packet, uint16 totalLength, uint8 protocol, uint32 source, uint32 destination) {
	packet[0] = 0x45;
	packet[1] = 0;
	WriteBe16(packet, 2, totalLength);
	WriteBe16(packet, 4, 0);
	WriteBe16(packet, 6, 0x4000);
	packet[8] = Ipv4DefaultTtl;
	packet[9] = protocol;
	WriteBe16(packet, 10, 0);
	WriteBe32(packet, 12, source);
	WriteBe32(packet, 16, destination);
	WriteBe16(packet, 10, InternetChecksum(packet.first(Ipv4MinHeaderSize), 0));
}


}
