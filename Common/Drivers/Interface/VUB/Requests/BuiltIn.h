#pragma once
#include <Drivers/Interface/VUB/Bytes.h>
#include <Drivers/Interface/VUB/Drops.h>
#include <Drivers/Interface/VUB/Requests/Body.h>
#include <Drivers/Interface/VUB/Requests/Wire.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/ResultStatus.h>
#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <span>
#include <type_traits>


namespace VUB {


// The built-in requests of the layer: each is a REQUEST with an id of 0xF0 or more, answered by the layer itself, with no command set and no
// handler. Ping and health are the two.
namespace BuiltInWire {
	inline constexpr uint8 IdPing = 0xF0;
	inline constexpr uint8 IdHealth = 0xF1;
	inline constexpr std::size_t PingStamp = 4;
	inline constexpr std::size_t PingRequest = RequestWire::RequestHeader + PingStamp;
	inline constexpr std::size_t PingReply = RequestWire::FinalHeader + PingStamp;
	inline constexpr std::size_t HealthRequest = RequestWire::RequestHeader;
	// The counters at the start of a health reply, 28 bytes on the wire. The user body follows them.
	inline constexpr std::size_t CountersBytes = 28;
	// The StackCounters at the start of those 28 bytes: the 22 bytes before the layer's three counts.
	inline constexpr std::size_t StackCountersBytes = 22;
	inline constexpr std::size_t HealthReplyHeader = RequestWire::FinalHeader + CountersBytes;
	inline constexpr uint32 DefaultTimeoutMs = 1000;
	inline constexpr uint32 MaxTimeoutMs = 2147483;
	// The command indexes of the requester slots that hold a ping and a health request. Commands are at most 240, so no command has an index
	// of FirstBuiltInSlot or more.
	inline constexpr uint8 PingSlotCommand = 0xFF;
	inline constexpr uint8 HealthSlotCommand = 0xFE;
	inline constexpr uint8 FirstBuiltInSlot = HealthSlotCommand;
}


// The handler of a ping: the result, and the round trip in microseconds (0 unless the result is ok).
using PingFn = void (*)(ResultStatus, uint32);


// The counters of a health reply, in the order they lie in memory and on the wire: the StackCounters of the responder (the eight drop
// counters, resends, peerRestarts, and the two high-water marks), then the layer's refused, lateReplies and eventsLost. The reply's 28 bytes
// are copied to and from this object as they lie, so the host is little-endian.
struct HealthCounters : StackCounters {
	uint16 refused = 0;
	uint16 lateReplies = 0;
	uint16 eventsLost = 0;
};


static_assert(std::is_trivially_copyable_v<HealthCounters>, "HealthCounters is copied as bytes, so it must be trivially copyable");
static_assert(sizeof(HealthCounters) == BuiltInWire::CountersBytes, "HealthCounters must be the 28 counter bytes of a health reply");
static_assert(sizeof(StackCounters) == BuiltInWire::StackCountersBytes, "StackCounters must be the 22 bytes before the layer's counts");
static_assert(std::endian::native == std::endian::little, "the health counters are copied as they lie: the host must be little-endian");


// The report of a health request. The user body U is NoBody when the responder has none. A report that ends locally (a deadline, a link
// loss, a restart) has the result set and the rest at its defaults.
template<typename U>
struct HealthReport {
	ResultStatus result = ResultStatus::ok;
	HealthCounters counters{};
	U user{};
};


// The handler type of a health request, void (*)(const HealthReport<U>&): valid is true for it, and UserType is its U.
template<typename Fn>
struct HealthHandlerOf {
	static constexpr bool valid = false;
	using UserType = NoBody;
};


template<typename U>
struct HealthHandlerOf<void (*)(const HealthReport<U>&)> {
	static constexpr bool valid = true;
	using UserType = U;
};


// The final handler of a ping slot. The request's Deliver passes the round trip as four little-endian bytes when the result is ok, and no
// bytes otherwise. The handler is the slot's final handler, so a ping has no handler of its own in the slot.
template<auto Handler>
void RunPing(ResultStatus result, uint8, std::span<const uint8> roundTrip) {
	uint32 roundTripUs = roundTrip.size() >= BuiltInWire::PingStamp ? GetLe32(roundTrip.data()) : 0;
	Handler(result, roundTripUs);
}


// The final handler of a health slot. The FINAL's body is the 28 counter bytes and then the user body. A reply that is not ok, or whose body
// is too short, has the result set and no counters. A local end has no body, so its report has only the result set.
template<auto Handler>
void RunHealth(ResultStatus result, uint8 status, std::span<const uint8> body) {
	using UserType = typename HealthHandlerOf<decltype(Handler)>::UserType;
	HealthReport<UserType> report{};
	report.result = result;
	if (result == ResultStatus::ok) {
		if (status != 0 || body.size() < BuiltInWire::CountersBytes) {
			report.result = ResultStatus::invalidData;
		} else {
			CopyBytes(body.data(), BuiltInWire::CountersBytes, reinterpret_cast<uint8*>(&report.counters));
			ResultStatus decoded = Decode(body.subspan(BuiltInWire::CountersBytes), report.user);
			if (decoded != ResultStatus::ok) {
				report.result = decoded;
			}
		}
	}

	Handler(report);
}


// The length of the largest packet of a ping request, or of its reply, for the layer's options: zero when the layer has no such ping.
constexpr std::size_t PingRequestBytes(bool sends, uint16 maxPadding) {
	return sends ? BuiltInWire::PingRequest + maxPadding : 0;
}


// The length of a health request, when the layer sends or answers one.
constexpr std::size_t HealthRequestBytes(bool used) {
	return used ? BuiltInWire::HealthRequest : 0;
}


// The length of a health reply with a user body of userBytes, when the layer sends or answers one.
constexpr std::size_t HealthReplyBytes(bool used, std::size_t userBytes) {
	return used ? BuiltInWire::HealthReplyHeader + userBytes : 0;
}


// The length of a ping reply with the padding of its maximum, zero when the layer does not answer pings.
constexpr std::size_t PingReplyBytes(bool answers, uint16 maxPadding) {
	return answers ? BuiltInWire::PingReply + maxPadding : 0;
}


// The bytes of the largest built-in reply of a layer: the ping reply with the padding of its maximum, or the health reply, whichever is
// larger. Zero when the layer answers neither. The one reply buffer of a responder holds the largest of them.
constexpr std::size_t BuiltInReplyBytes(bool answersPing, uint16 maxPadding, bool answersHealth, std::size_t userBytes) {
	return std::max(PingReplyBytes(answersPing, maxPadding), HealthReplyBytes(answersHealth, userBytes));
}


// The one reply buffer of a responder for its built-in requests. A reply is taken in Take and sent from Process; held is true while it
// waits. A health reply waits with length 0 until its fill runs; every other reply has its length when it is held.
template<std::size_t Bytes>
struct BuiltInReply {
	std::array<uint8, Bytes> bytes{};
	std::size_t length = 0;
	bool held = false;
};


// The reply buffer of a responder, as a base class: none when the layer answers no built-in request, so it takes no storage.
template<std::size_t Bytes>
struct BuiltInReplyHolder {
	BuiltInReply<Bytes> builtInReply{};
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


// Writes the header of a health reply into the reply buffer, in Take: the kind, the number, the tag, code ok and status 0. The body is written
// by the fill, so the reply holds with length 0 until the fill writes it.
template<std::size_t Bytes>
void StartHealthReply(BuiltInReply<Bytes>& reply, uint16 number, uint8 tag) {
	reply.bytes[0] = RequestWire::KindFinal;
	PutLe16(reply.bytes.data() + 1, number);
	reply.bytes[3] = tag;
	reply.bytes[4] = RequestWire::CodeOk;
	reply.bytes[5] = 0;
	reply.length = 0;
	reply.held = true;
}


// The fill of a health reply's user body: the fill writes the body, which is then encoded into the bytes of the reply.
template<typename U, void (*Fill)(U&)>
void EncodeHealthBody(std::span<uint8> bytes) {
	U value{};
	Fill(value);
	(void)Encode(value, bytes);
}


// The fill of a health reply as a variable, which an options value holds (see Reliable.h for the reason).
template<void (*Fn)(std::span<uint8>)>
inline constexpr void (*HealthFillHandler)(std::span<uint8>) = Fn;


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


// Writes a health REQUEST: the kind, the id, the number and the tag. It has no body. Returns the packet's length.
inline std::size_t EncodeHealthRequest(std::span<uint8> packet, uint16 number, uint8 tag) {
	packet[0] = RequestWire::KindRequest;
	packet[1] = BuiltInWire::IdHealth;
	PutLe16(packet.data() + 2, number);
	packet[4] = tag;
	return BuiltInWire::HealthRequest;
}


}
