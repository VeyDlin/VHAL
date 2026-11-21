#pragma once
#include <DemoUSBNetwork/Network/EthernetFrame.h>
#include <DemoUSBNetwork/Network/Ipv4Packet.h>
#include <DemoUSBNetwork/Network/NetworkBytes.h>
#include <Utilities/DataTypes.h>
#include <algorithm>
#include <cstddef>
#include <span>


// ARP (RFC 826) for IPv4 over Ethernet: the request that a PC sends to learn the device's MAC address, and the reply the device gives.
namespace DemoNetwork {


constexpr std::size_t ArpPacketSize = 28;
constexpr std::size_t ArpFrameSize = EthernetHeaderSize + ArpPacketSize;
constexpr uint16 ArpOperationRequest = 1;
constexpr uint16 ArpOperationReply = 2;


struct ArpMessage {
	uint16 operation = 0;
	MacAddress senderMac{};
	uint32 senderAddress = 0;
	MacAddress targetMac{};
	uint32 targetAddress = 0;
};


// Parses an ARP packet for IPv4 over Ethernet: hardware type 1, protocol 0x0800, hardware length 6 and protocol length 4. False for
// anything else, or when the packet is shorter than 28 bytes.
inline bool ParseArp(std::span<const uint8> packet, ArpMessage& message) {
	if (packet.size() < ArpPacketSize) {
		return false;
	}

	if (ReadBe16(packet, 0) != 1 || ReadBe16(packet, 2) != EtherTypeIpv4 || packet[4] != 6 || packet[5] != 4) {
		return false;
	}

	message.operation = ReadBe16(packet, 6);
	std::copy_n(packet.begin() + 8, 6, message.senderMac.begin());
	message.senderAddress = ReadBe32(packet, 14);
	std::copy_n(packet.begin() + 18, 6, message.targetMac.begin());
	message.targetAddress = ReadBe32(packet, 24);
	return true;
}


// True for a request that asks for the device's address and comes from a host that has one: a probe (sender address 0.0.0.0) is not
// answered, and neither is a sender whose MAC is a group address.
inline bool IsArpRequestFor(const ArpMessage& message, uint32 address) {
	return message.operation == ArpOperationRequest
		&& message.targetAddress == address
		&& IsUnicastAddress(message.senderAddress)
		&& !IsGroupMac(message.senderMac);
}


// Writes the 42-byte ARP reply frame to request into frame: the device (deviceMac, deviceAddress) is the sender, and the frame goes to
// the requester's MAC. Returns the frame length. frame must hold ArpFrameSize bytes.
inline std::size_t WriteArpReply(std::span<uint8> frame, const MacAddress& deviceMac, uint32 deviceAddress, const ArpMessage& request) {
	WriteEthernetHeader(frame, request.senderMac, deviceMac, EtherTypeArp);
	std::span<uint8> packet = frame.subspan(EthernetHeaderSize, ArpPacketSize);
	WriteBe16(packet, 0, 1);
	WriteBe16(packet, 2, EtherTypeIpv4);
	packet[4] = 6;
	packet[5] = 4;
	WriteBe16(packet, 6, ArpOperationReply);
	std::copy(deviceMac.begin(), deviceMac.end(), packet.begin() + 8);
	WriteBe32(packet, 14, deviceAddress);
	std::copy(request.senderMac.begin(), request.senderMac.end(), packet.begin() + 18);
	WriteBe32(packet, 24, request.senderAddress);
	return ArpFrameSize;
}


}
