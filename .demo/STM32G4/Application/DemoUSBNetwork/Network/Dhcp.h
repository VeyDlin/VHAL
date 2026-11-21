#pragma once
#include <DemoProducts.h>
#include <DemoUSBNetwork/Network/EthernetFrame.h>
#include <DemoUSBNetwork/Network/Ipv4Packet.h>
#include <DemoUSBNetwork/Network/NetworkBytes.h>
#include <DemoUSBNetwork/Network/UdpPacket.h>
#include <Utilities/DataTypes.h>
#include <algorithm>
#include <cstddef>
#include <span>


// A minimal DHCP server (RFC 2131 with the BOOTP layout of RFC 2131 section 2) for one lease: the host gets the leased address from a
// DISCOVER (OFFER) and a REQUEST for it (ACK). Nothing else is answered. The server keeps no state, because it has one address to give.
// Limitation: option overload (option 52, which continues the options in the sname and file fields) is not supported. The parser reads
// the options field only, so a client that puts its message type or requested address in an overloaded field gets no answer.
namespace DemoNetwork {


constexpr uint16 DhcpServerPort = 67;
constexpr uint16 DhcpClientPort = 68;
constexpr std::size_t DhcpFixedSize = 236;
constexpr std::size_t DhcpCookieSize = 4;
// The fixed part, the cookie, the message type option (3 bytes) and the end option: the shortest message the parser takes.
constexpr std::size_t DhcpMessageMinSize = DhcpFixedSize + DhcpCookieSize + 3 + 1;
// BOOTP messages are 300 bytes at least, so the reply is padded with zeros to that length.
constexpr std::size_t DhcpReplySize = 300;
constexpr uint8 DhcpOpRequest = 1;
constexpr uint8 DhcpOpReply = 2;
constexpr uint8 DhcpHardwareEthernet = 1;
constexpr uint8 DhcpHardwareLength = 6;
constexpr uint32 DhcpMagicCookie = 0x63825363;
constexpr uint16 DhcpBroadcastFlag = 0x8000;
// The lease time the offer and the ACK give, in seconds (one hour).
constexpr uint32 DhcpLeaseSeconds = 3600;

constexpr uint8 DhcpMessageDiscover = 1;
constexpr uint8 DhcpMessageOffer = 2;
constexpr uint8 DhcpMessageRequest = 3;
constexpr uint8 DhcpMessageAck = 5;

constexpr uint8 DhcpOptionPad = 0;
constexpr uint8 DhcpOptionSubnetMask = 1;
constexpr uint8 DhcpOptionRequestedAddress = 50;
constexpr uint8 DhcpOptionLeaseTime = 51;
constexpr uint8 DhcpOptionMessageType = 53;
constexpr uint8 DhcpOptionServerIdentifier = 54;
constexpr uint8 DhcpOptionEnd = 255;


// A client message that passed ParseDhcp(). The options the server uses are kept; the others are skipped.
struct DhcpMessage {
	uint8 messageType = 0;
	uint32 transactionId = 0;
	uint16 flags = 0;
	uint32 clientAddress = 0;
	uint32 relayAddress = 0;
	MacAddress clientMac{};
	bool hasRequestedAddress = false;
	uint32 requestedAddress = 0;
	bool hasServerIdentifier = false;
	uint32 serverIdentifier = 0;
};


// What the server sends for a message.
enum class DhcpAnswer {
	none,
	offer,
	ack
};


// Parses a client message (the UDP payload). The op must be BOOTREQUEST, the hardware type Ethernet with 6 bytes, and the magic cookie
// right. The options must end with the end option, each must lie inside the message, the message type must be present and 1 byte, and
// the requested address and server identifier must be 4 bytes. False for any of these.
inline bool ParseDhcp(std::span<const uint8> message, DhcpMessage& result) {
	if (message.size() < DhcpMessageMinSize) {
		return false;
	}

	if (message[0] != DhcpOpRequest || message[1] != DhcpHardwareEthernet || message[2] != DhcpHardwareLength) {
		return false;
	}

	if (ReadBe32(message, DhcpFixedSize) != DhcpMagicCookie) {
		return false;
	}

	result.transactionId = ReadBe32(message, 4);
	result.flags = ReadBe16(message, 10);
	result.clientAddress = ReadBe32(message, 12);
	result.relayAddress = ReadBe32(message, 24);
	std::copy_n(message.begin() + 28, 6, result.clientMac.begin());

	bool sawMessageType = false;
	std::size_t offset = DhcpFixedSize + DhcpCookieSize;
	while (offset < message.size()) {
		uint8 code = message[offset];
		if (code == DhcpOptionPad) {
			offset++;
			continue;
		}

		if (code == DhcpOptionEnd) {
			return sawMessageType;
		}

		if (offset + 1 >= message.size()) {
			return false;
		}

		std::size_t length = message[offset + 1];
		if (offset + 2 + length > message.size()) {
			return false;
		}

		std::span<const uint8> value = message.subspan(offset + 2, length);
		if (code == DhcpOptionMessageType) {
			if (length != 1) {
				return false;
			}

			result.messageType = value[0];
			sawMessageType = true;
		} else if (code == DhcpOptionRequestedAddress) {
			if (length != 4) {
				return false;
			}

			result.hasRequestedAddress = true;
			result.requestedAddress = ReadBe32(value, 0);
		} else if (code == DhcpOptionServerIdentifier) {
			if (length != 4) {
				return false;
			}

			result.hasServerIdentifier = true;
			result.serverIdentifier = ReadBe32(value, 0);
		}

		offset += 2 + length;
	}

	// The message ended without the end option.
	return false;
}


// The answer to a parsed message. A DISCOVER gets an OFFER of the one lease. A REQUEST gets an ACK when it asks for the leased address,
// in option 50 or, when the client renews, in ciaddr; a REQUEST that names another server gets nothing. Relayed messages (giaddr set)
// and every other message type get nothing.
inline DhcpAnswer AnswerDhcp(const DhcpMessage& message, uint32 deviceAddress, uint32 leasedAddress) {
	if (message.relayAddress != 0) {
		return DhcpAnswer::none;
	}

	if (message.messageType == DhcpMessageDiscover) {
		return DhcpAnswer::offer;
	}

	if (message.messageType == DhcpMessageRequest) {
		if (message.hasServerIdentifier && message.serverIdentifier != deviceAddress) {
			return DhcpAnswer::none;
		}

		uint32 address = message.hasRequestedAddress ? message.requestedAddress : message.clientAddress;
		if (address == leasedAddress) {
			return DhcpAnswer::ack;
		}
	}

	return DhcpAnswer::none;
}


// Writes an option of one byte value as code, length 1 and the value. Returns the offset after it.
inline std::size_t PutOption8(std::span<uint8> message, std::size_t offset, uint8 code, uint8 value) {
	message[offset] = code;
	message[offset + 1] = 1;
	message[offset + 2] = value;
	return offset + 3;
}


// Writes an option of a 4-byte value as code, length 4 and the value, most significant byte first. Returns the offset after it.
inline std::size_t PutOption32(std::span<uint8> message, std::size_t offset, uint8 code, uint32 value) {
	message[offset] = code;
	message[offset + 1] = 4;
	WriteBe32(message, offset + 2, value);
	return offset + 6;
}


// Writes the BOOTREPLY for an offer or an ack into message, which holds DhcpReplySize bytes. It answers request: the transaction, the
// flags, ciaddr and the client's MAC are echoed. yiaddr is the leased address, siaddr the server, and the options give the message type,
// the server identifier, the lease time and the subnet mask. The rest is zero. Returns DhcpReplySize.
inline std::size_t WriteDhcpReply(
	std::span<uint8> message,
	const DhcpMessage& request,
	DhcpAnswer answer,
	uint32 deviceAddress,
	uint32 leasedAddress)
{
	std::fill(message.begin(), message.end(), 0);
	message[0] = DhcpOpReply;
	message[1] = DhcpHardwareEthernet;
	message[2] = DhcpHardwareLength;
	WriteBe32(message, 4, request.transactionId);
	WriteBe16(message, 10, request.flags);
	WriteBe32(message, 12, request.clientAddress);
	WriteBe32(message, 16, leasedAddress);
	WriteBe32(message, 20, deviceAddress);
	WriteBe32(message, 24, request.relayAddress);
	std::copy(request.clientMac.begin(), request.clientMac.end(), message.begin() + 28);
	WriteBe32(message, DhcpFixedSize, DhcpMagicCookie);

	uint8 messageType = answer == DhcpAnswer::ack ? DhcpMessageAck : DhcpMessageOffer;
	std::size_t offset = DhcpFixedSize + DhcpCookieSize;
	offset = PutOption8(message, offset, DhcpOptionMessageType, messageType);
	offset = PutOption32(message, offset, DhcpOptionServerIdentifier, deviceAddress);
	offset = PutOption32(message, offset, DhcpOptionLeaseTime, DhcpLeaseSeconds);
	offset = PutOption32(message, offset, DhcpOptionSubnetMask, DemoProducts::NetworkNetmask);
	message[offset] = DhcpOptionEnd;
	return DhcpReplySize;
}


// Builds the complete reply frame to a parsed client message, with the answer AnswerDhcp() gave it: Ethernet, IPv4 and UDP around the
// BOOTREPLY. The destination follows RFC 2131 section 4.1: the client's address when it has one (a renewal, unicast to its MAC), else
// broadcast when the client set the broadcast flag, else the leased address with the client's MAC. Returns the frame length, or 0 when
// the answer is none or frame is too small.
inline std::size_t BuildDhcpReplyFrame(
	std::span<uint8> frame,
	const MacAddress& deviceMac,
	uint32 deviceAddress,
	const DhcpMessage& request,
	DhcpAnswer answer,
	uint32 leasedAddress)
{
	if (answer == DhcpAnswer::none) {
		return 0;
	}

	const std::size_t udpLength = UdpHeaderSize + DhcpReplySize;
	const std::size_t frameSize = EthernetHeaderSize + Ipv4MinHeaderSize + udpLength;
	if (frame.size() < frameSize) {
		return 0;
	}

	MacAddress destinationMac = request.clientMac;
	uint32 destinationAddress = leasedAddress;
	if (request.clientAddress != 0) {
		destinationAddress = request.clientAddress;
	} else if ((request.flags & DhcpBroadcastFlag) != 0) {
		destinationMac = BroadcastMac;
		destinationAddress = BroadcastAddress;
	}

	WriteEthernetHeader(frame, destinationMac, deviceMac, EtherTypeIpv4);
	std::span<uint8> packet = frame.subspan(EthernetHeaderSize, frameSize - EthernetHeaderSize);
	WriteIpv4Header(packet, static_cast<uint16>(Ipv4MinHeaderSize + udpLength), IpProtocolUdp, deviceAddress, destinationAddress);
	std::span<uint8> datagram = packet.subspan(Ipv4MinHeaderSize, udpLength);
	WriteDhcpReply(datagram.subspan(UdpHeaderSize, DhcpReplySize), request, answer, deviceAddress, leasedAddress);
	WriteUdpDatagram(datagram, DhcpServerPort, DhcpClientPort, deviceAddress, destinationAddress);
	return frameSize;
}


}
