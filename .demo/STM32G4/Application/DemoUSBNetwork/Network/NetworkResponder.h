#pragma once
#include <DemoProducts.h>
#include <DemoUSBNetwork/Network/Arp.h>
#include <DemoUSBNetwork/Network/Dhcp.h>
#include <DemoUSBNetwork/Network/EthernetFrame.h>
#include <DemoUSBNetwork/Network/IcmpEcho.h>
#include <DemoUSBNetwork/Network/Ipv4Packet.h>
#include <DemoUSBNetwork/Network/UdpPacket.h>
#include <Utilities/DataTypes.h>
#include <cstddef>
#include <span>


// The device's answers to the frames a host sends on the link: ARP for the device address, ICMP echo to it, and DHCP for the one lease.
// Everything else is dropped without an answer. The responder is pure: it reads a received frame and writes at most one reply frame, so
// the demo's main loop and the host tests call the same code. It keeps no state; the counters are the caller's.
namespace DemoNetwork {


// The device's identity on the link: its own MAC address (the device side of the pair DemoProducts derives), its IPv4 address and the
// address its DHCP server leases.
struct NetworkIdentity {
	MacAddress mac{};
	uint32 address = DemoProducts::NetworkDeviceAddress;
	uint32 leasedAddress = DemoProducts::NetworkLeasedAddress;
};


// What the responder did with the frames it was given. arpReplies, icmpReplies, dhcpOffers and dhcpAcks count the answers. dropped
// counts every frame without an answer; malformed counts those among them that broke a rule of their protocol (a bad length, a bad
// checksum, a bad option), as opposed to frames that are valid but not for the device.
struct NetworkCounters {
	uint32 arpReplies = 0;
	uint32 icmpReplies = 0;
	uint32 dhcpOffers = 0;
	uint32 dhcpAcks = 0;
	uint32 dropped = 0;
	uint32 malformed = 0;
};


enum class FrameVerdict {
	answered,
	ignored,
	malformed
};


// The answer to an ICMP echo request to the device. The request's payload is copied into the reply, so the request and the reply must
// not share their storage.
inline FrameVerdict AnswerIcmp(
	const EthernetHeader& header,
	const Ipv4Packet& packet,
	std::span<uint8> reply,
	const NetworkIdentity& identity,
	NetworkCounters& counters,
	std::size_t& replyLength)
{
	if (packet.destination != identity.address || header.destination != identity.mac) {
		return FrameVerdict::ignored;
	}

	if (!IsIcmpEchoRequest(packet.payload)) {
		return FrameVerdict::ignored;
	}

	if (!IcmpChecksumValid(packet.payload)) {
		return FrameVerdict::malformed;
	}

	if (!IsUnicastAddress(packet.source)) {
		return FrameVerdict::ignored;
	}

	const std::size_t messageSize = packet.payload.size();
	const std::size_t frameSize = EthernetHeaderSize + Ipv4MinHeaderSize + messageSize;
	if (frameSize > EthernetMaxFrameSize || reply.size() < frameSize) {
		return FrameVerdict::ignored;
	}

	WriteEthernetHeader(reply, header.source, identity.mac, EtherTypeIpv4);
	WriteIpv4Header(
		reply.subspan(EthernetHeaderSize),
		static_cast<uint16>(Ipv4MinHeaderSize + messageSize),
		IpProtocolIcmp,
		identity.address,
		packet.source);
	WriteIcmpEchoReply(packet.payload, reply.subspan(EthernetHeaderSize + Ipv4MinHeaderSize, messageSize));
	replyLength = frameSize;
	counters.icmpReplies++;
	return FrameVerdict::answered;
}


// The answer to a UDP datagram for the DHCP server: the datagram must come from a client port to the server port, sent to the device
// address or to the broadcast address. Its checksum and its DHCP message are checked before the answer is decided.
inline FrameVerdict AnswerUdp(
	const EthernetHeader& header,
	const Ipv4Packet& packet,
	std::span<uint8> reply,
	const NetworkIdentity& identity,
	NetworkCounters& counters,
	std::size_t& replyLength)
{
	bool toDevice = packet.destination == identity.address || packet.destination == BroadcastAddress;
	if (!toDevice || (!IsBroadcastMac(header.destination) && header.destination != identity.mac)) {
		return FrameVerdict::ignored;
	}

	UdpDatagram datagram;
	if (!ParseUdp(packet.payload, packet.source, packet.destination, datagram)) {
		return FrameVerdict::malformed;
	}

	if (datagram.destinationPort != DhcpServerPort || datagram.sourcePort != DhcpClientPort) {
		return FrameVerdict::ignored;
	}

	DhcpMessage message;
	if (!ParseDhcp(datagram.payload, message)) {
		return FrameVerdict::malformed;
	}

	DhcpAnswer answer = AnswerDhcp(message, identity.address, identity.leasedAddress);
	if (answer == DhcpAnswer::none) {
		return FrameVerdict::ignored;
	}

	replyLength = BuildDhcpReplyFrame(reply, identity.mac, identity.address, message, answer, identity.leasedAddress);
	if (replyLength == 0) {
		return FrameVerdict::ignored;
	}

	if (answer == DhcpAnswer::offer) {
		counters.dhcpOffers++;
	} else {
		counters.dhcpAcks++;
	}

	return FrameVerdict::answered;
}


// Classifies one frame and writes its answer into reply, when it has one. Only the verdict and the reply length leave this function.
inline FrameVerdict AnswerFrameBody(
	std::span<const uint8> frame,
	std::span<uint8> reply,
	const NetworkIdentity& identity,
	NetworkCounters& counters,
	std::size_t& replyLength)
{
	EthernetHeader header;
	if (!ParseEthernetHeader(frame, header)) {
		return FrameVerdict::malformed;
	}

	std::span<const uint8> payload = frame.subspan(EthernetHeaderSize);
	if (header.type == EtherTypeArp) {
		ArpMessage message;
		if (!ParseArp(payload, message)) {
			return FrameVerdict::malformed;
		}

		if (!IsArpRequestFor(message, identity.address) || reply.size() < ArpFrameSize) {
			return FrameVerdict::ignored;
		}

		replyLength = WriteArpReply(reply, identity.mac, identity.address, message);
		counters.arpReplies++;
		return FrameVerdict::answered;
	}

	if (header.type != EtherTypeIpv4) {
		return FrameVerdict::ignored;
	}

	Ipv4Packet packet;
	if (!ParseIpv4(payload, packet)) {
		return FrameVerdict::malformed;
	}

	if (packet.protocol == IpProtocolIcmp) {
		return AnswerIcmp(header, packet, reply, identity, counters, replyLength);
	}

	if (packet.protocol == IpProtocolUdp) {
		return AnswerUdp(header, packet, reply, identity, counters, replyLength);
	}

	return FrameVerdict::ignored;
}


// Answers one received frame. Returns the length of the reply written into reply, or 0 when the frame gets no answer. The reply buffer
// must hold EthernetMaxFrameSize bytes, which covers every reply the device builds. A frame without an answer is counted as dropped, and
// as malformed when it broke a rule.
inline std::size_t AnswerFrame(
	std::span<const uint8> frame,
	std::span<uint8> reply,
	const NetworkIdentity& identity,
	NetworkCounters& counters)
{
	std::size_t replyLength = 0;
	FrameVerdict verdict = AnswerFrameBody(frame, reply, identity, counters, replyLength);
	if (verdict == FrameVerdict::answered) {
		return replyLength;
	}

	counters.dropped++;
	if (verdict == FrameVerdict::malformed) {
		counters.malformed++;
	}

	return 0;
}


}
