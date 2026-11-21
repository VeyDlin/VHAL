#pragma once
#include <DemoUSBNetwork/Network/InternetChecksum.h>
#include <DemoUSBNetwork/Network/NetworkBytes.h>
#include <Utilities/DataTypes.h>
#include <algorithm>
#include <cstddef>
#include <span>


// ICMP echo (RFC 792): the request a PC's ping sends and the reply the device gives. The checksum covers the whole ICMP message, from
// the type on, including the payload, so it is checked on the message as received.
namespace DemoNetwork {


constexpr uint8 IcmpTypeEchoReply = 0;
constexpr uint8 IcmpTypeEchoRequest = 8;
constexpr std::size_t IcmpEchoHeaderSize = 8;


// True for an echo request: the message is at least 8 bytes, with type 8 and code 0. The checksum is checked separately.
inline bool IsIcmpEchoRequest(std::span<const uint8> message) {
	return message.size() >= IcmpEchoHeaderSize && message[0] == IcmpTypeEchoRequest && message[1] == 0;
}


// True when the message's checksum is right: the sum over the whole message, checksum field included, is 0xFFFF.
inline bool IcmpChecksumValid(std::span<const uint8> message) {
	return OnesComplementSum(message, 0) == 0xFFFF;
}


// Writes the echo reply to request into reply: type 0 and code 0, the identifier, the sequence number and the payload copied, and the
// checksum computed. reply must be as long as request; the caller checks that, and that request is a valid echo request.
inline void WriteIcmpEchoReply(std::span<const uint8> request, std::span<uint8> reply) {
	std::copy(request.begin(), request.end(), reply.begin());
	reply[0] = IcmpTypeEchoReply;
	reply[1] = 0;
	WriteBe16(reply, 2, 0);
	WriteBe16(reply, 2, InternetChecksum(reply, 0));
}


}
