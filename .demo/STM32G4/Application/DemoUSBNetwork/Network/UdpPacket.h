#pragma once
#include <DemoUSBNetwork/Network/InternetChecksum.h>
#include <DemoUSBNetwork/Network/Ipv4Packet.h>
#include <DemoUSBNetwork/Network/NetworkBytes.h>
#include <Utilities/DataTypes.h>
#include <cstddef>
#include <span>


// UDP (RFC 768) over IPv4: the header with its checksum over the pseudo header, which carries the addresses of the IPv4 packet.
namespace DemoNetwork {


constexpr std::size_t UdpHeaderSize = 8;


// A datagram that passed ParseUdp(). payload is the bytes after the 8-byte header, up to the UDP length.
struct UdpDatagram {
	uint16 sourcePort = 0;
	uint16 destinationPort = 0;
	std::span<const uint8> payload{};
};


// Checks a UDP datagram of an IPv4 packet from source to destination (the IPv4 payload). The length field must be 8 or more and lie
// inside the datagram; the bytes after it are ignored. A checksum of 0 means that the sender computed none, which IPv4 allows, and is
// accepted. Any other checksum must match the pseudo header and the datagram. False for any of these.
inline bool ParseUdp(std::span<const uint8> datagram, uint32 source, uint32 destination, UdpDatagram& result) {
	if (datagram.size() < UdpHeaderSize) {
		return false;
	}

	uint16 length = ReadBe16(datagram, 4);
	if (length < UdpHeaderSize || length > datagram.size()) {
		return false;
	}

	if (ReadBe16(datagram, 6) != 0) {
		uint32 initial = PseudoHeaderSum(source, destination, IpProtocolUdp, length);
		if (OnesComplementSum(datagram.first(length), initial) != 0xFFFF) {
			return false;
		}
	}

	result.sourcePort = ReadBe16(datagram, 0);
	result.destinationPort = ReadBe16(datagram, 2);
	result.payload = datagram.subspan(UdpHeaderSize, length - UdpHeaderSize);
	return true;
}


// Writes the UDP header and checksum of a datagram whose payload is already in place after the header. The whole datagram (header and
// payload) is the span, so its size is the UDP length. source and destination are the IPv4 addresses of the pseudo header. A computed
// checksum of 0 is sent as 0xFFFF, as RFC 768 asks, so it is never taken for "no checksum".
inline void WriteUdpDatagram(std::span<uint8> datagram, uint16 sourcePort, uint16 destinationPort, uint32 source, uint32 destination) {
	WriteBe16(datagram, 0, sourcePort);
	WriteBe16(datagram, 2, destinationPort);
	WriteBe16(datagram, 4, static_cast<uint16>(datagram.size()));
	WriteBe16(datagram, 6, 0);
	uint32 initial = PseudoHeaderSum(source, destination, IpProtocolUdp, static_cast<uint16>(datagram.size()));
	uint16 checksum = InternetChecksum(datagram, initial);
	if (checksum == 0) {
		checksum = 0xFFFF;
	}

	WriteBe16(datagram, 6, checksum);
}


}
