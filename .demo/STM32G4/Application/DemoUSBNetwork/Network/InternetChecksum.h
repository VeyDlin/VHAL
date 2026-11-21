#pragma once
#include <Utilities/DataTypes.h>
#include <cstddef>
#include <span>


// The Internet checksum (RFC 1071): the one's complement sum of 16-bit big-endian words. A trailing odd byte is the high byte of a
// word whose low byte is zero. The sum is folded back to 16 bits, so a header that carries its own checksum sums to 0xFFFF when it
// is right. The header checksums of IPv4, ICMP and UDP are this sum.
namespace DemoNetwork {


// The folded one's complement sum of bytes, started from initial (the pseudo header sum for UDP, 0 otherwise). Not inverted.
inline uint16 OnesComplementSum(std::span<const uint8> bytes, uint32 initial) {
	uint32 sum = initial;
	std::size_t index = 0;
	while (index + 1 < bytes.size()) {
		sum += (static_cast<uint32>(bytes[index]) << 8) | static_cast<uint32>(bytes[index + 1]);
		index += 2;
	}

	if (index < bytes.size()) {
		sum += static_cast<uint32>(bytes[index]) << 8;
	}

	while ((sum >> 16) != 0) {
		sum = (sum & 0xFFFF) + (sum >> 16);
	}

	return static_cast<uint16>(sum);
}


// The value of a checksum field for bytes whose field is zero: the complement of their sum.
inline uint16 InternetChecksum(std::span<const uint8> bytes, uint32 initial) {
	return static_cast<uint16>(~OnesComplementSum(bytes, initial));
}


}
