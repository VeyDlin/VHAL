#pragma once
#include <Drivers/Interface/VUB/Bytes.h>
#include <Drivers/Interface/VUB/Requests/Wire.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/ResultStatus.h>
#include <array>
#include <cstddef>
#include <span>


namespace VUB {


// The built-in requests of the layer: each is a REQUEST with an id of 0xF0 or more, answered by the layer itself, with no command set and no
// handler. Ping is the only one.
namespace BuiltInWire {
	inline constexpr uint8 IdPing = 0xF0;
	inline constexpr std::size_t PingStamp = 4;
	inline constexpr std::size_t PingRequest = RequestWire::RequestHeader + PingStamp;
	inline constexpr std::size_t PingReply = RequestWire::FinalHeader + PingStamp;
	inline constexpr uint32 DefaultTimeoutMs = 1000;
	inline constexpr uint32 MaxTimeoutMs = 2147483;
	// The command index of a requester slot that holds a ping. Commands are at most 240, so no command has this index.
	inline constexpr uint8 PingSlotCommand = 0xFF;
}


// The handler of a ping: the result, and the round trip in microseconds (0 unless the result is ok).
using PingFn = void (*)(ResultStatus, uint32);


// The final handler of a ping slot. The request's Deliver passes the round trip as four little-endian bytes when the result is ok, and no
// bytes otherwise. The handler is the slot's final handler, so a ping has no handler of its own in the slot.
template<auto Handler>
void RunPing(ResultStatus result, uint8, std::span<const uint8> roundTrip) {
	uint32 roundTripUs = roundTrip.size() >= BuiltInWire::PingStamp ? GetLe32(roundTrip.data()) : 0;
	Handler(result, roundTripUs);
}


// The length of the largest packet of a ping request, or of its reply, for the layer's options: zero when the layer has no such ping.
constexpr std::size_t PingRequestBytes(bool sends, uint16 maxPadding) {
	return sends ? BuiltInWire::PingRequest + maxPadding : 0;
}


// The bytes of the largest built-in reply of a layer: the ping reply with the padding of its maximum. Zero when the layer does not answer
// pings. A further built-in reply adds its size here, so the one reply buffer holds the largest of them.
constexpr std::size_t BuiltInReplyBytes(bool answers, uint16 maxPadding) {
	return answers ? BuiltInWire::PingReply + maxPadding : 0;
}


// The one reply buffer of a responder for its built-in requests. A reply is taken in Take and sent from Process; held is true while it waits.
template<std::size_t Bytes>
struct BuiltInReply {
	std::array<uint8, Bytes> bytes{};
	std::size_t length = 0;
	bool held = false;
};


// The reply buffer of a responder, as a base class: none when the layer does not answer pings, so it takes no storage.
template<std::size_t Bytes>
struct BuiltInReplyHolder {
	BuiltInReply<Bytes> pingReply{};
};


template<>
struct BuiltInReplyHolder<0> {
};


// Writes the FINAL of a ping request into the reply buffer: the kind, the number and the tag, code ok, status 0, then the stamp and the
// padding, copied from the request. The request has its header and its stamp at least, and fits the buffer.
template<std::size_t Bytes>
void EncodePingReply(BuiltInReply<Bytes>& reply, std::span<const uint8> request) {
	reply.bytes[0] = RequestWire::KindFinal;
	reply.bytes[1] = request[2];
	reply.bytes[2] = request[3];
	reply.bytes[3] = request[4];
	reply.bytes[4] = RequestWire::CodeOk;
	reply.bytes[5] = 0;
	for (std::size_t index = RequestWire::RequestHeader; index < request.size(); index++) {
		reply.bytes[index + 1] = request[index];
	}

	reply.length = request.size() + 1;
	reply.held = true;
}


// Writes a ping REQUEST: the kind, the id, the number, the tag, the stamp (the sender's clock) and the padding, whose bytes are the low byte of
// each index. Returns the packet's length.
inline std::size_t EncodePingRequest(std::span<uint8> packet, uint16 number, uint8 tag, uint32 stampUs, uint16 padding) {
	packet[0] = RequestWire::KindRequest;
	packet[1] = BuiltInWire::IdPing;
	PutLe16(packet.data() + 2, number);
	packet[4] = tag;
	PutLe32(packet.data() + RequestWire::RequestHeader, stampUs);
	for (uint16 index = 0; index < padding; index++) {
		packet[BuiltInWire::PingRequest + index] = static_cast<uint8>(index);
	}

	return BuiltInWire::PingRequest + padding;
}


}
