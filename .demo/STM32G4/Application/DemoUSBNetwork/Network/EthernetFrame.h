#pragma once
#include <DemoUSBNetwork/Network/NetworkBytes.h>
#include <Utilities/DataTypes.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <span>


// Ethernet II framing: a 14-byte header (destination, source and EtherType) and the payload. The frames are the NCM datagrams: Ethernet
// frames without the FCS, 14 to 1514 bytes. A VLAN-tagged frame has another EtherType, so it is not answered and is dropped.
namespace DemoNetwork {


constexpr std::size_t EthernetHeaderSize = 14;
constexpr std::size_t EthernetMaxFrameSize = 1514;
constexpr uint16 EtherTypeIpv4 = 0x0800;
constexpr uint16 EtherTypeArp = 0x0806;


using MacAddress = std::array<uint8, 6>;


inline constexpr MacAddress BroadcastMac = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };


struct EthernetHeader {
	MacAddress destination{};
	MacAddress source{};
	uint16 type = 0;
};


// True for the broadcast address. Its first bit is set, as for every group address, so IsGroupMac() covers it too.
inline bool IsBroadcastMac(const MacAddress& address) {
	return std::equal(address.begin(), address.end(), BroadcastMac.begin());
}


// True for a group (multicast or broadcast) address: bit 0 of the first byte is set. Such an address is never a sender.
inline bool IsGroupMac(const MacAddress& address) {
	return (address[0] & 0x01) != 0;
}


// Reads the header of a frame. False when the frame is shorter than the header.
inline bool ParseEthernetHeader(std::span<const uint8> frame, EthernetHeader& header) {
	if (frame.size() < EthernetHeaderSize) {
		return false;
	}

	std::copy_n(frame.begin(), 6, header.destination.begin());
	std::copy_n(frame.begin() + 6, 6, header.source.begin());
	header.type = ReadBe16(frame, 12);
	return true;
}


// Writes the 14-byte header at the start of frame. The caller gives a frame of at least 14 bytes.
inline void WriteEthernetHeader(std::span<uint8> frame, const MacAddress& destination, const MacAddress& source, uint16 type) {
	std::copy(destination.begin(), destination.end(), frame.begin());
	std::copy(source.begin(), source.end(), frame.begin() + 6);
	WriteBe16(frame, 12, type);
}


}
