#include "Chains.h"
#include "ReliableChains.h"
#include "SimCheck.h"
#include <Drivers/Interface/VUB/Requests.h>
#include <Utilities/DataTypes.h>
#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <utility>

// The health request of the requests layer over a Reliable link, two stacks on a reporting packet line. The device serves Command::hold and
// health, and sends Command::echo and an alarm event (Event::alarm). The requester sends Command::hold, Command::unknown, a raw id 0x78 that neither side knows, and
// health. The expected bytes are design section 5.6 and 9.2 (payloads before Reliable's header); the counter values come from the events each
// test makes. The layer counters refused, lateReplies and eventsLost are in HealthReport's counters (design section 0, decisions 10 and 14).


namespace HealthCheck {


// The status of the probe commands, one byte on the wire.
enum class ProbeStatus : uint8 {
	done = 0,
	held = 1
};


// The request body of the probe commands.
struct HealthProbe {
	uint8 mode = 0;
};


// The body of the alarm event.
struct HealthAlarm {
	uint8 code = 0;
};


// The user part of the health reply: six bytes (design section 9.2).
struct HealthBody {
	uint32 uptimeS = 0;
	uint8 resetReason = 0;
	uint8 state = 0;
};


}


namespace VUB {


template<>
struct BodyOf<HealthCheck::HealthProbe> {
	static consteval auto Layout() {
		return Fields<HealthCheck::HealthProbe>()
			.Field<&HealthCheck::HealthProbe::mode>();
	}
};


template<>
struct BodyOf<HealthCheck::HealthAlarm> {
	static consteval auto Layout() {
		return Fields<HealthCheck::HealthAlarm>()
			.Field<&HealthCheck::HealthAlarm::code>();
	}
};


template<>
struct BodyOf<HealthCheck::HealthBody> {
	static consteval auto Layout() {
		return Fields<HealthCheck::HealthBody>()
			.Field<&HealthCheck::HealthBody::uptimeS>()
			.Field<&HealthCheck::HealthBody::resetReason>()
			.Field<&HealthCheck::HealthBody::state>();
	}
};


}


namespace HealthCheck {


// A user body ten bytes long: longer than the device's HealthBody, which the asking side of the two-role layer uses.
struct HealthWideBody {
	uint32 uptimeS = 0;
	uint32 bootCount = 0;
	uint16 spare = 0;
};


// A user body four bytes long: shorter than the device's HealthBody, whose first four bytes are the uptime.
struct HealthShortBody {
	uint32 uptimeS = 0;
};


}


namespace VUB {


template<>
struct BodyOf<HealthCheck::HealthWideBody> {
	static consteval auto Layout() {
		return Fields<HealthCheck::HealthWideBody>()
			.Field<&HealthCheck::HealthWideBody::uptimeS>()
			.Field<&HealthCheck::HealthWideBody::bootCount>()
			.Field<&HealthCheck::HealthWideBody::spare>();
	}
};


template<>
struct BodyOf<HealthCheck::HealthShortBody> {
	static consteval auto Layout() {
		return Fields<HealthCheck::HealthShortBody>()
			.Field<&HealthCheck::HealthShortBody::uptimeS>();
	}
};


}


// The command ids and the event ids of this file. The value is the id on the wire.
enum class Command : uint8 {
	hold = 0x21,
	echo = 0x22,
	unknown = 0x77
};


enum class Event : uint8 {
	alarm = 0x40
};


// The command set of the test: two probes with a ticket on the responder, an id that the device serves no handler for, and a delivered event.
using HealthCommands = VUB::CommandSet<+[]() {
	return VUB::Commands()
		.Command<Command::hold>()
			.Request<HealthCheck::HealthProbe>()
			.Reply<HealthCheck::ProbeStatus>()
			.ReplyTimeoutMs(5000)
			.TicketTimeoutMs(60000)
		.Command<Command::echo>()
			.Request<HealthCheck::HealthProbe>()
			.Reply<HealthCheck::ProbeStatus>()
			.ReplyTimeoutMs(1000)
			.TicketTimeoutMs(60000)
		.Command<Command::unknown>()
			.Reply<HealthCheck::ProbeStatus>()
			.ReplyTimeoutMs(2000)
		.Event<Event::alarm>()
			.Body<HealthCheck::HealthAlarm>()
			.Delivered();
}>;


using HoldTicket = VUB::Ticket<HealthCommands, Command::hold>;
using EchoTicket = VUB::Ticket<HealthCommands, Command::echo>;
using ProbeFinal = VUB::Final<HealthCheck::ProbeStatus, VUB::NoBody>;


// The device's state: the held probe ticket, and what the fill and the requests of the device saw.
inline std::optional<HoldTicket> heldHold;
inline std::size_t holdSeen = 0;
inline std::size_t healthFills = 0;
inline std::size_t echoFinals = 0;
inline ResultStatus lastEchoResult = ResultStatus::ok;


// The requester's state: the probe finals, the echo ticket it holds, and the health reports it got.
inline std::optional<EchoTicket> heldEcho;
inline std::size_t echoSeen = 0;
inline std::size_t holdFinals = 0;
inline ResultStatus lastHoldResult = ResultStatus::ok;
inline std::size_t unknownFinals = 0;
inline ResultStatus lastUnknownResult = ResultStatus::ok;
inline std::size_t healthReports = 0;
inline std::size_t healthOks = 0;
inline std::size_t healthBusies = 0;
inline VUB::HealthReport<HealthCheck::HealthBody> lastHealth{};
inline std::size_t nothingReports = 0;
inline ResultStatus lastNothingResult = ResultStatus::ok;


void OnHold(HoldTicket ticket, const HealthCheck::HealthProbe& body) {
	(void)body;
	holdSeen++;
	heldHold = ticket;
}


void OnEcho(EchoTicket ticket, const HealthCheck::HealthProbe& body) {
	(void)body;
	echoSeen++;
	heldEcho = ticket;
}


// The fill of the device's health reply: the uptime is the test clock in seconds, the reset reason 1 and the state 2.
void FillHealth(HealthCheck::HealthBody& body) {
	healthFills++;
	body.uptimeS = static_cast<uint32>(testNowUs / 1000000);
	body.resetReason = 1;
	body.state = 2;
}


// The fill of a device whose health reply has no user body.
void FillNothing(VUB::NoBody&) {
	healthFills++;
}


void OnEchoFinal(const ProbeFinal& reply) {
	echoFinals++;
	lastEchoResult = reply.result;
}


void OnHoldFinal(const ProbeFinal& reply) {
	holdFinals++;
	lastHoldResult = reply.result;
}


void OnUnknownFinal(const ProbeFinal& reply) {
	unknownFinals++;
	lastUnknownResult = reply.result;
}


void OnHealth(const VUB::HealthReport<HealthCheck::HealthBody>& report) {
	healthReports++;
	lastHealth = report;
	if (report.result == ResultStatus::ok) {
		healthOks++;
	}

	if (report.result == ResultStatus::busy) {
		healthBusies++;
	}
}


void OnHealthNothing(const VUB::HealthReport<VUB::NoBody>& report) {
	nothingReports++;
	lastNothingResult = report.result;
}


using DeviceHandlers = VUB::Handlers<
	VUB::On<Command::hold, &OnHold>>;


using PcHandlers = VUB::Handlers<
	VUB::On<Command::echo, &OnEcho>>;


// The device with the health reply and the counters of the requests layer. One requester slot, one ticket, a refusal ring of two.
using HealthDevice = VUB::Stack<+[]() {
	return VUB::Builder()
		.CountDrops()
		.SendQueue(4)
		.Layer<VUB::Requests<HealthCommands, DeviceHandlers>>().Open(1).Tickets(1).Refusals(2).Counted()
			.AnswerHealth<HealthCheck::HealthBody, &FillHealth>()
		.Layer<VUB::ReliableOf<40, 4>>().Counted().Retries(3).TimeoutMs(200).Window(4)
		.Transform<XorTransform>()
		.Link<ReportingPacketEnd>()
		.HighWater()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


// The device of the health reply without a user body.
using NothingDevice = VUB::Stack<+[]() {
	return VUB::Builder()
		.CountDrops()
		.SendQueue(4)
		.Layer<VUB::Requests<HealthCommands, DeviceHandlers>>().Open(1).Tickets(1).Counted()
			.AnswerHealth<VUB::NoBody, &FillNothing>()
		.Layer<VUB::ReliableOf<40, 4>>().Counted().Retries(3).TimeoutMs(200).Window(4)
		.Transform<XorTransform>()
		.Link<ReportingPacketEnd>()
		.HighWater()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


// A device with no health reply: a health request to it is refused.
using NoHealthDevice = VUB::Stack<+[]() {
	return VUB::Builder()
		.CountDrops()
		.SendQueue(4)
		.Layer<VUB::Requests<HealthCommands, DeviceHandlers>>().Open(1).Tickets(1)
		.Layer<VUB::ReliableOf<40, 4>>().Retries(3).TimeoutMs(200).Window(4)
		.Transform<XorTransform>()
		.Link<ReportingPacketEnd>()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


// The requester: four slots, one ticket for the echo, and the health request with the user body.
using HealthPc = VUB::Stack<+[]() {
	return VUB::Builder()
		.CountDrops()
		.SendQueue(4)
		.Layer<VUB::Requests<HealthCommands, PcHandlers>>().Open(4).Tickets(1)
			.Health<HealthCheck::HealthBody>()
		.Layer<VUB::ReliableOf<40, 4>>().Retries(3).TimeoutMs(200).Window(4)
		.Transform<XorTransform>()
		.Link<ReportingPacketEnd>()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


// The requester of a device with no user body.
using NothingPc = VUB::Stack<+[]() {
	return VUB::Builder()
		.CountDrops()
		.SendQueue(4)
		.Layer<VUB::Requests<HealthCommands, PcHandlers>>().Open(1).Tickets(1)
			.Health<VUB::NoBody>()
		.Layer<VUB::ReliableOf<40, 4>>().Retries(3).TimeoutMs(200).Window(4)
		.Transform<XorTransform>()
		.Link<ReportingPacketEnd>()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


using HealthPcHandle = decltype(std::declval<HealthPc&>().Requests());
using HealthDeviceHandle = decltype(std::declval<HealthDevice&>().Requests());
using HealthDeviceRequests = VUB::Requests<HealthCommands, DeviceHandlers>;
using NothingPcHandle = decltype(std::declval<NothingPc&>().Requests());


// The packets a round carried in each direction: toFar is the requester's, toNear the responder's.
struct WireLog {
	ReliableCapture toFar;
	ReliableCapture toNear;


	void Clear() {
		toFar = ReliableCapture{};
		toNear = ReliableCapture{};
	}
};


// Passes what a line delivers on to a stack, and keeps a copy of each packet.
template<typename Stack>
struct Tee {
	ReliableCapture* capture;
	Stack* target;


	void Feed(uint8) {
	}


	void FeedPacket(std::span<const uint8> packet) {
		capture->FeedPacket(packet);
		target->FeedPacket(packet);
	}
};


// A sink that takes what the line delivers and passes none of it on: those packets are lost.
struct DropSink {
	void Feed(uint8) {
	}


	void FeedPacket(std::span<const uint8>) {
	}
};


// The sentinel of TailIs: no byte of the tail is left out of the check.
inline constexpr std::size_t noIgnoredByte = static_cast<std::size_t>(-1);


// True when the packet of index, with the XOR transform undone, ends with the expected tail. The line carries the transformed bytes: each
// byte is XORed with its key (by its position in the Reliable packet), and the two check bytes at the end are verified first. The tail
// positions ignoredA and ignoredB are not checked.
template<std::size_t Size>
bool TailIs(const ReliableCapture& capture, std::size_t index, const std::array<uint8, Size>& tail, std::size_t ignoredA = noIgnoredByte, std::size_t ignoredB = noIgnoredByte) {
	if (index >= capture.count || index >= capture.packets.size() || capture.lengths[index] > 64 || capture.lengths[index] < XorTransform::growth + Size) {
		return false;
	}

	std::size_t length = capture.lengths[index] - XorTransform::growth;
	std::array<uint8, 64> decoded{};
	uint8 sum = 0;
	uint8 check = 0;
	for (std::size_t at = 0; at < length; at++) {
		uint8 value = capture.packets[index][at];
		sum = static_cast<uint8>(sum + value);
		check = static_cast<uint8>(check ^ value);
		decoded[at] = static_cast<uint8>(value ^ XorTransform::Key(at));
	}

	if (sum != capture.packets[index][length] || check != capture.packets[index][length + 1]) {
		return false;
	}

	std::size_t start = length - Size;
	for (std::size_t byte = 0; byte < Size; byte++) {
		if (byte != ignoredA && byte != ignoredB && decoded[start + byte] != tail[byte]) {
			return false;
		}
	}

	return true;
}


// The number of packets of the capture whose layer payload ends with the tail. The line also carries Reliable's own packets (acknowledgements,
// and the repeats Reliable sends of a packet), so a count of the capture's packets is not a count of the layer's.
template<std::size_t Size>
std::size_t CountTailIs(const ReliableCapture& capture, const std::array<uint8, Size>& tail, std::size_t ignoredA = noIgnoredByte, std::size_t ignoredB = noIgnoredByte) {
	std::size_t found = 0;
	for (std::size_t index = 0; index < capture.count && index < capture.packets.size(); index++) {
		if (TailIs(capture, index, tail, ignoredA, ignoredB)) {
			found++;
		}
	}

	return found;
}


// True when some packet the capture holds ends with the tail. Used where the number of packets of a round depends on the acknowledgements.
template<std::size_t Size>
bool AnyTailIs(const ReliableCapture& capture, const std::array<uint8, Size>& tail) {
	for (std::size_t index = 0; index < capture.count; index++) {
		if (TailIs(capture, index, tail)) {
			return true;
		}
	}

	return false;
}


// One round: the requester runs, its packets reach the responder, the responder runs, its packets reach the requester, the requester runs.
template<typename NearStack, typename FarStack>
void RoundOf(ReportingPacketLine& line, NearStack& nearStack, FarStack& farStack, WireLog& log, uint32 nowUs) {
	testNowUs = nowUs;
	log.Clear();
	nearStack.Process();
	Tee<FarStack> forward{&log.toFar, &farStack};
	line.DeliverToB(forward);
	farStack.Process();
	Tee<NearStack> back{&log.toNear, &nearStack};
	line.DeliverToA(back);
	nearStack.Process();
}


// Rounds 50 ms apart: each acknowledgement reaches its sender before a resend is due (the resend timeout is 200 ms).
template<typename NearStack, typename FarStack>
void Settle(ReportingPacketLine& line, NearStack& nearStack, FarStack& farStack, WireLog& log, uint32 startUs, uint32 rounds) {
	for (uint32 round = 0; round < rounds; round++) {
		RoundOf(line, nearStack, farStack, log, startUs + round * 50000);
	}
}


// A round in which the responder's packets are lost on the line, so its Reliable gets no acknowledgement and resends.
template<typename NearStack, typename FarStack>
void DroppedRound(ReportingPacketLine& line, NearStack& nearStack, FarStack& farStack, uint32 nowUs) {
	testNowUs = nowUs;
	nearStack.Process();
	farStack.Process();
	DropSink dropped;
	line.DeliverToA(dropped);
	nearStack.Process();
}


// The error of a Command::unknown send: the send is refused with no packet and no number used. Command::unknown has no request body, so the
// call passes VUB::NoBody{}, as the other tests of a command with no request body do.
template<typename HandleType>
ResultStatus UnknownError(HandleType& handle) {
	return handle.template Send<Command::unknown, &OnUnknownFinal>(VUB::NoBody{}).Error();
}


template<typename HandleType>
bool SendUnknown(HandleType& handle) {
	return handle.template Send<Command::unknown, &OnUnknownFinal>(VUB::NoBody{}).IsErr() == false;
}


template<typename HandleType>
bool SendHold(HandleType& handle) {
	return handle.template Send<Command::hold, &OnHoldFinal>(HealthCheck::HealthProbe{}).IsErr() == false;
}


template<typename HandleType>
bool SendEcho(HandleType& handle) {
	return handle.template Send<Command::echo, &OnEchoFinal>(HealthCheck::HealthProbe{}).IsErr() == false;
}


template<typename HandleType>
bool EmitAlarm(HandleType& handle) {
	return handle.template Emit<Event::alarm>(HealthCheck::HealthAlarm{7}) == ResultStatus::ok;
}


// The sessions of both ends: the first send of each end starts its session and is busy until the sync is acknowledged.
template<typename NearStack, typename FarStack>
void Establish(ReportingPacketLine& line, NearStack& nearStack, FarStack& farStack, WireLog& log, uint32 startUs) {
	RoundOf(line, nearStack, farStack, log, startUs);
	decltype(nearStack.Requests()) handle = nearStack.Requests();
	SIM_EXPECT(UnknownError(handle) == ResultStatus::busy);
	std::array<uint8, 1> trigger = {0xEE};
	SIM_EXPECT(farStack.template LayerOutbox<0>().Send(std::span<const uint8>(trigger.data(), trigger.size())) == ResultStatus::busy);
	Settle(line, nearStack, farStack, log, startUs + 50000, 3);
}


// The session of a requester that restarted: only its own session starts again, as the responder's session is still up.
template<typename NearStack, typename FarStack>
void ReEstablish(ReportingPacketLine& line, NearStack& nearStack, FarStack& farStack, WireLog& log, uint32 startUs) {
	RoundOf(line, nearStack, farStack, log, startUs);
	decltype(nearStack.Requests()) handle = nearStack.Requests();
	SIM_EXPECT(UnknownError(handle) == ResultStatus::busy);
	Settle(line, nearStack, farStack, log, startUs + 50000, 3);
}


void ResetHealthState() {
	ResetReliableState();
	heldHold.reset();
	holdSeen = 0;
	healthFills = 0;
	echoFinals = 0;
	lastEchoResult = ResultStatus::ok;
	heldEcho.reset();
	echoSeen = 0;
	holdFinals = 0;
	lastHoldResult = ResultStatus::ok;
	unknownFinals = 0;
	lastUnknownResult = ResultStatus::ok;
	healthReports = 0;
	healthOks = 0;
	healthBusies = 0;
	lastHealth = VUB::HealthReport<HealthCheck::HealthBody>{};
	nothingReports = 0;
	lastNothingResult = ResultStatus::ok;
}


// A reply of the health request with the counters zero and the user body uptime 60 s, reset 1, state 2 (40 bytes). The two high-water marks
// (tail indexes 32 and 33) are not checked: the health request itself went through the receive ring, so the ring's mark is at least 1, and
// the design gives no value for the control queue under this stack (an open point of the stage 3 report).
void TestZeroCountersReplyBytes() {
	ResetHealthState();
	ReportingPacketLine line(VUB::Faults{});
	HealthPc pc(line.a);
	HealthDevice device(line.b);
	WireLog log;
	GiveSession(pc);
	GiveSession(device);
	Establish(line, pc, device, log, 1000);

	HealthPcHandle handle = pc.Requests();
	SIM_EXPECT(handle.Health<&OnHealth>() == ResultStatus::ok);
	RoundOf(line, pc, device, log, 60000000);

	// Design 5.1: the health request has no body. It is request number 1 with tag 1 (the busy probe of Establish used no number).
	std::array<uint8, 5> request = {0x01, 0xF1, 0x01, 0x00, 0x01};
	SIM_EXPECT(CountTailIs(log.toFar, request) >= 1);
	SIM_EXPECT(TailIs(log.toFar, 0, request));

	// Design 5.6 and 9.2: the final of number 1 with status 0, 28 zero counter bytes, then uptime 60 s (3C 00 00 00), reset 1 and state 2.
	std::array<uint8, 40> reply = {
		0x03, 0x01, 0x00, 0x01, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x3C, 0x00, 0x00, 0x00, 0x01, 0x02
	};
	SIM_EXPECT(CountTailIs(log.toNear, reply, 26, 27) >= 1);
	SIM_EXPECT(TailIs(log.toNear, 0, reply, 26, 27));
	SIM_EXPECT(healthReports == 1);
	SIM_EXPECT(lastHealth.result == ResultStatus::ok);
	SIM_EXPECT(lastHealth.user.uptimeS == 60);
	SIM_EXPECT(lastHealth.user.resetReason == 1);
	SIM_EXPECT(lastHealth.user.state == 2);
	SIM_EXPECT(lastHealth.counters.drops.queueFull == 0);
	SIM_EXPECT(lastHealth.counters.drops.transformError == 0);
	SIM_EXPECT(lastHealth.counters.drops.layerError == 0);
	SIM_EXPECT(lastHealth.counters.resends == 0);
	SIM_EXPECT(lastHealth.counters.refused == 0);
	SIM_EXPECT(lastHealth.counters.lateReplies == 0);
	SIM_EXPECT(lastHealth.counters.eventsLost == 0);
	SIM_EXPECT(healthFills == 1);
}


// A responder whose health reply has no user body: the reply is the 28 counter bytes after the 6-byte final header (34 bytes).
void TestReplyWithoutUserBody() {
	ResetHealthState();
	ReportingPacketLine line(VUB::Faults{});
	NothingPc pc(line.a);
	NothingDevice device(line.b);
	WireLog log;
	GiveSession(pc);
	GiveSession(device);
	Establish(line, pc, device, log, 1000);

	NothingPcHandle handle = pc.Requests();
	SIM_EXPECT(handle.Health<&OnHealthNothing>() == ResultStatus::ok);
	RoundOf(line, pc, device, log, 60000000);

	std::array<uint8, 34> reply = {
		0x03, 0x01, 0x00, 0x01, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
	};
	SIM_EXPECT(CountTailIs(log.toNear, reply, 26, 27) >= 1);
	SIM_EXPECT(nothingReports == 1);
	SIM_EXPECT(lastNothingResult == ResultStatus::ok);
	SIM_EXPECT(healthFills == 1);
}


// A responder with no health reply refuses the health request as notSupported (design 5.2, code 2).
void TestResponderWithoutHealthIsNotSupported() {
	ResetHealthState();
	ReportingPacketLine line(VUB::Faults{});
	HealthPc pc(line.a);
	NoHealthDevice device(line.b);
	WireLog log;
	GiveSession(pc);
	GiveSession(device);
	Establish(line, pc, device, log, 1000);

	HealthPcHandle handle = pc.Requests();
	SIM_EXPECT(handle.Health<&OnHealth>() == ResultStatus::ok);
	RoundOf(line, pc, device, log, 60000000);

	std::array<uint8, 5> request = {0x01, 0xF1, 0x01, 0x00, 0x01};
	std::array<uint8, 5> refused = {0x03, 0x01, 0x00, 0x01, 0x02};
	SIM_EXPECT(TailIs(log.toFar, 0, request));
	SIM_EXPECT(CountTailIs(log.toNear, refused) >= 1);
	SIM_EXPECT(TailIs(log.toNear, 0, refused));
	SIM_EXPECT(healthReports == 1);
	SIM_EXPECT(lastHealth.result == ResultStatus::notSupported);
}


// A request with an id that neither side serves (0x78, in no command set): the device refuses it notSupported with no layer error, and
// the refusal is answered (it is not counted as refused, which is for a ring that is full). This is a raw packet below the requests layer.
void TestUnknownCommandIdIsNotSupported() {
	ResetHealthState();
	ReportingPacketLine line(VUB::Faults{});
	HealthPc pc(line.a);
	HealthDevice device(line.b);
	WireLog log;
	GiveSession(pc);
	GiveSession(device);
	Establish(line, pc, device, log, 1000);

	// A REQUEST with id 0x78, number 0x55, tag 0x55 and no body (design 5.1).
	std::array<uint8, 5> raw = {0x01, 0x78, 0x55, 0x00, 0x55};
	SIM_EXPECT(pc.LayerOutbox<0>().Send(std::span<const uint8>(raw.data(), raw.size())) == ResultStatus::ok);
	RoundOf(line, pc, device, log, 60000000);

	// Design 5.2, code 2: the refusal FINAL of number 0x55 with its tag.
	std::array<uint8, 5> refused = {0x03, 0x55, 0x00, 0x55, 0x02};
	SIM_EXPECT(CountTailIs(log.toFar, raw) >= 1);
	SIM_EXPECT(TailIs(log.toFar, 0, raw));
	SIM_EXPECT(CountTailIs(log.toNear, refused) >= 1);
	SIM_EXPECT(TailIs(log.toNear, 0, refused));
	SIM_EXPECT(device.Drops().layerError == 0);
	SIM_EXPECT(device.Find<HealthDeviceRequests>().Refused() == 0);
}


// The second built-in request while the first reply is unsent is refused busy (design section 0, decision 13). Both requests come in one
// walk of the device: the first takes the layer's one built-in reply buffer, the second is refused busy from the ring (code 4).
void TestSecondBuiltInIsBusy() {
	ResetHealthState();
	ReportingPacketLine line(VUB::Faults{});
	HealthPc pc(line.a);
	HealthDevice device(line.b);
	WireLog log;
	GiveSession(pc);
	GiveSession(device);
	Establish(line, pc, device, log, 1000);

	HealthPcHandle handle = pc.Requests();
	SIM_EXPECT(handle.Health<&OnHealth>() == ResultStatus::ok);
	SIM_EXPECT(handle.Health<&OnHealth>() == ResultStatus::ok);
	RoundOf(line, pc, device, log, 60000000);

	// The refusal of number 2 (tag 2) carries code 4. The order of the two finals on the line is not given, so either index may hold it.
	std::array<uint8, 5> busy = {0x03, 0x02, 0x00, 0x02, 0x04};
	SIM_EXPECT(AnyTailIs(log.toNear, busy));
	SIM_EXPECT(healthFills == 1);
	SIM_EXPECT(healthReports == 2);
	SIM_EXPECT(healthOks == 1);
	SIM_EXPECT(healthBusies == 1);
	SIM_EXPECT(device.Find<HealthDeviceRequests>().Refused() == 0);
}


// A reply too short for the requester's user body (34 bytes, where 40 are needed) is invalidData at the requester.
void TestShortReplyIsInvalidData() {
	ResetHealthState();
	ReportingPacketLine line(VUB::Faults{});
	HealthPc pc(line.a);
	NothingDevice device(line.b);
	WireLog log;
	GiveSession(pc);
	GiveSession(device);
	Establish(line, pc, device, log, 1000);

	HealthPcHandle handle = pc.Requests();
	SIM_EXPECT(handle.Health<&OnHealth>() == ResultStatus::ok);
	RoundOf(line, pc, device, log, 60000000);

	SIM_EXPECT(healthReports == 1);
	SIM_EXPECT(lastHealth.result == ResultStatus::invalidData);
}


// The health request takes no ticket: the one ticket of the device is held by a probe, and the health request is still answered.
void TestHealthDoesNotTakeTicket() {
	ResetHealthState();
	ReportingPacketLine line(VUB::Faults{});
	HealthPc pc(line.a);
	HealthDevice device(line.b);
	WireLog log;
	GiveSession(pc);
	GiveSession(device);
	Establish(line, pc, device, log, 1000);

	HealthPcHandle handle = pc.Requests();
	SIM_EXPECT(SendHold(handle));
	RoundOf(line, pc, device, log, 60000000);
	SIM_EXPECT(heldHold.has_value());
	SIM_EXPECT(heldHold->Open());

	SIM_EXPECT(handle.Health<&OnHealth>() == ResultStatus::ok);
	RoundOf(line, pc, device, log, 60050000);
	SIM_EXPECT(healthReports == 1);
	SIM_EXPECT(lastHealth.result == ResultStatus::ok);
	SIM_EXPECT(healthFills == 1);

	// The probe's ticket is still open, and the device finishes it afterwards.
	SIM_EXPECT(heldHold->Open());
	SIM_EXPECT(heldHold->Finish(HealthCheck::ProbeStatus::done) == ResultStatus::ok);
	RoundOf(line, pc, device, log, 60100000);
	SIM_EXPECT(holdFinals == 1);
	SIM_EXPECT(lastHoldResult == ResultStatus::ok);
}


// The fill runs once per health request, from the device's Process: not when the request is taken into the layer.
void TestFillRunsOnceFromProcess() {
	ResetHealthState();
	ReportingPacketLine line(VUB::Faults{});
	HealthPc pc(line.a);
	HealthDevice device(line.b);
	WireLog log;
	GiveSession(pc);
	GiveSession(device);
	Establish(line, pc, device, log, 1000);

	HealthPcHandle handle = pc.Requests();
	SIM_EXPECT(handle.Health<&OnHealth>() == ResultStatus::ok);
	testNowUs = 60000000;
	pc.Process();
	Tee<HealthDevice> forward{&log.toFar, &device};
	line.DeliverToB(forward);
	SIM_EXPECT(healthFills == 0);
	device.Process();
	SIM_EXPECT(healthFills == 1);
	Tee<HealthPc> back{&log.toNear, &pc};
	line.DeliverToA(back);
	SIM_EXPECT(healthReports == 0);
	pc.Process();
	SIM_EXPECT(healthReports == 1);

	// A second request: a second fill.
	SIM_EXPECT(handle.Health<&OnHealth>() == ResultStatus::ok);
	RoundOf(line, pc, device, log, 60050000);
	SIM_EXPECT(healthFills == 2);
	SIM_EXPECT(healthReports == 2);
}


// Every counter at a value set by a real event, in one reply. The layout is the memory order of HealthCounters: 8 drop counters, resends,
// peerRestarts (2 bytes each), highWaterRing and highWaterControl (1 byte each), then refused, lateReplies, eventsLost (2 bytes each), and
// the user body. The three layer counters are
// read through the device's handle as well as on its layer.
void TestCountersInReplyAfterRealEvents() {
	ResetHealthState();
	ReportingPacketLine line(VUB::Faults{});
	std::optional<HealthPc> pcBox;
	pcBox.emplace(line.a);
	HealthPc& pc = *pcBox;
	HealthDevice device(line.b);
	WireLog log;
	GiveSession(pc);
	GiveSession(device);
	Establish(line, pc, device, log, 1000);

	// eventsLost and resends: the device emits a delivered alarm while every packet to the requester is lost. Its Reliable resends three
	// times (Retries(3)), then reports retryExhausted, and the layer counts the lost event.
	HealthDeviceHandle deviceHandle = device.Requests();
	testNowUs = 1000000;
	SIM_EXPECT(EmitAlarm(deviceHandle));
	DroppedRound(line, pc, device, 1300000);
	DroppedRound(line, pc, device, 1600000);
	DroppedRound(line, pc, device, 1900000);
	DroppedRound(line, pc, device, 2200000);
	DroppedRound(line, pc, device, 2500000);
	DroppedRound(line, pc, device, 2800000);
	SIM_EXPECT(device.Find<ReliableLayer>().Resends() == 3);
	SIM_EXPECT(device.Find<HealthDeviceRequests>().EventsLost() == 1);
	SIM_EXPECT(deviceHandle.EventsLost() == 1);
	Settle(line, pc, device, log, 3000000, 2);

	// peerRestarts: the requester restarts with a new boot value. Its new sync is the device's restart report.
	pcBox.reset();
	pcBox.emplace(line.a);
	HealthPc& restarted = *pcBox;
	GiveSession(restarted, 0x2345);
	ReEstablish(line, restarted, device, log, 3200000);
	SIM_EXPECT(device.Find<ReliableLayer>().PeerRestarts() == 1);

	// refused: three unknown commands in one walk of the device. Its refusal ring holds two, so the third is refused and not answered.
	HealthPcHandle restartedHandle = restarted.Requests();
	SIM_EXPECT(SendUnknown(restartedHandle));
	SIM_EXPECT(SendUnknown(restartedHandle));
	SIM_EXPECT(SendUnknown(restartedHandle));
	RoundOf(line, restarted, device, log, 3400000);
	// The two refusals in the ring are not sent in this round: each send of the device is busy, so the refusal waits in the ring until a send
	// succeeds (design 6.4). The requester's handlers see both finals after the settling rounds.
	SIM_EXPECT(device.Find<HealthDeviceRequests>().Refused() == 1);
	SIM_EXPECT(deviceHandle.Refused() == 1);
	Settle(line, restarted, device, log, 3450000, 2);
	SIM_EXPECT(unknownFinals == 2);
	SIM_EXPECT(lastUnknownResult == ResultStatus::notSupported);

	// layerError: a packet whose first byte is no kind (0xEE) is sent below the requests layer. The device refuses it with no reply and
	// counts it in layerError (design 5.1).
	std::array<uint8, 2> junk = {0xEE, 0x00};
	SIM_EXPECT(restarted.LayerOutbox<0>().Send(std::span<const uint8>(junk.data(), junk.size())) == ResultStatus::ok);
	RoundOf(line, restarted, device, log, 3550000);
	SIM_EXPECT(device.Drops().layerError == 1);

	// queueFull and transformError: seven packets with a bad check arrive before the device runs. Four fit the receive queue, and three
	// are counted as queueFull. The four held packets fail their transform check when the device runs.
	std::array<uint8, 3> badCheck = {0x00, 0x00, 0x01};
	testNowUs = 3600000;
	for (uint32 index = 0; index < 7; index++) {
		device.FeedPacket(std::span<const uint8>(badCheck.data(), badCheck.size()));
	}

	device.Process();
	SIM_EXPECT(device.Drops().queueFull == 3);
	SIM_EXPECT(device.Drops().transformError == 4);

	// lateReplies: the device sends a probe that the requester holds. The device's deadline passes first (timeout), and the requester's
	// final then arrives for a number the device has closed.
	testNowUs = 3700000;
	SIM_EXPECT(SendEcho(deviceHandle));
	RoundOf(line, restarted, device, log, 3700000);
	SIM_EXPECT(heldEcho.has_value());
	Settle(line, restarted, device, log, 3750000, 25);
	SIM_EXPECT(echoFinals == 1);
	SIM_EXPECT(lastEchoResult == ResultStatus::timeout);
	SIM_EXPECT(heldEcho->Open());
	SIM_EXPECT(heldEcho->Finish(HealthCheck::ProbeStatus::done) == ResultStatus::ok);
	RoundOf(line, restarted, device, log, 5000000);
	SIM_EXPECT(echoFinals == 1);
	SIM_EXPECT(device.Find<HealthDeviceRequests>().LateReplies() == 1);
	SIM_EXPECT(deviceHandle.LateReplies() == 1);

	// The health request, at a clock of 60 s, so the uptime in the fill is 60. Its reply carries every counter.
	SIM_EXPECT(restartedHandle.Health<&OnHealth>() == ResultStatus::ok);
	RoundOf(line, restarted, device, log, 60000000);

	// Bytes of the final of number 4 (the three unknown commands took 1 to 3), tag 4, in the order of memory: the drop counters (queueFull 3,
	// transformError 4, layerError 1), resends 3, peerRestarts 1, highWaterRing 4, highWaterControl (packet index 27, not checked: the design
	// gives no value for the control queue under this stack), then refused 1, lateReplies 1, eventsLost 1.
	std::array<uint8, 40> reply = {
		0x03, 0x04, 0x00, 0x04, 0x00, 0x00,
		0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x03, 0x00, 0x01, 0x00, 0x04, 0x00, 0x01, 0x00, 0x01, 0x00, 0x01, 0x00,
		0x3C, 0x00, 0x00, 0x00, 0x01, 0x02
	};
	SIM_EXPECT(CountTailIs(log.toNear, reply, 27) >= 1);
	SIM_EXPECT(TailIs(log.toNear, 0, reply, 27));
	SIM_EXPECT(healthReports == 1);
	SIM_EXPECT(lastHealth.result == ResultStatus::ok);
	SIM_EXPECT(lastHealth.counters.drops.queueFull == 3);
	SIM_EXPECT(lastHealth.counters.drops.transformError == 4);
	SIM_EXPECT(lastHealth.counters.drops.layerError == 1);
	SIM_EXPECT(lastHealth.counters.resends == 3);
	SIM_EXPECT(lastHealth.counters.peerRestarts == 1);
	SIM_EXPECT(lastHealth.counters.highWaterRing == 4);
	SIM_EXPECT(lastHealth.counters.refused == 1);
	SIM_EXPECT(lastHealth.counters.lateReplies == 1);
	SIM_EXPECT(lastHealth.counters.eventsLost == 1);
	SIM_EXPECT(lastHealth.user.uptimeS == 60);
	SIM_EXPECT(lastHealth.user.resetReason == 1);
	SIM_EXPECT(lastHealth.user.state == 2);
	SIM_EXPECT(healthFills == 1);
}


// The counters are 16 bits and stop at 65535: 70000 packets arrive at a full queue. The queue holds the health request and three bad packets.
void TestCountersSaturate() {
	ResetHealthState();
	ReportingPacketLine line(VUB::Faults{});
	HealthPc pc(line.a);
	HealthDevice device(line.b);
	WireLog log;
	GiveSession(pc);
	GiveSession(device);
	Establish(line, pc, device, log, 1000);

	HealthPcHandle handle = pc.Requests();
	SIM_EXPECT(handle.Health<&OnHealth>() == ResultStatus::ok);
	testNowUs = 60000000;
	log.Clear();
	pc.Process();
	Tee<HealthDevice> forward{&log.toFar, &device};
	line.DeliverToB(forward);
	// The four slots of the device's queue take the packets the line delivers first: the health request, and the Reliable packets of the line
	// (a repeat of the request, and the acknowledgements), whichever they are. The bad packets that follow fill the rest.
	std::size_t queued = log.toFar.count < 4 ? log.toFar.count : 4;

	std::array<uint8, 3> badCheck = {0x00, 0x00, 0x01};
	for (uint32 index = 0; index < 70000; index++) {
		device.FeedPacket(std::span<const uint8>(badCheck.data(), badCheck.size()));
	}

	device.Process();
	Tee<HealthPc> back{&log.toNear, &pc};
	line.DeliverToA(back);
	pc.Process();

	SIM_EXPECT(device.Drops().queueFull == 65535);
	SIM_EXPECT(device.Drops().transformError == 4 - queued);
	SIM_EXPECT(healthReports == 1);
	SIM_EXPECT(lastHealth.counters.drops.queueFull == 65535);
	SIM_EXPECT(lastHealth.counters.drops.transformError == 4 - queued);
	SIM_EXPECT(lastHealth.counters.highWaterRing == 4);
}


// --- snippet: Layer/Requests.md, health ---
// The asking side: two slots, and a health request whose reply carries a HealthBody. The handler gets the report.
using HealthAsker = VUB::Stack<+[]() {
	return VUB::Builder()
		.CountDrops()
		.SendQueue(4)
		.Layer<VUB::Requests<HealthCommands, PcHandlers>>().Open(2).Tickets(1)
			.Health<HealthCheck::HealthBody>()
		.Layer<VUB::ReliableOf<40, 4>>().Retries(3).TimeoutMs(200).Window(4)
		.Transform<XorTransform>()
		.Link<ReportingPacketEnd>()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


// The handler of the report: the result, the responder's counters, and its user body.
void OnHealthReport(const VUB::HealthReport<HealthCheck::HealthBody>& report) {
	(void)report;
}


// A health request. The report goes to OnHealthReport in the Process that takes the reply, or with a local result.
inline ResultStatus AskHealth(HealthAsker& asker) {
	return asker.Requests().Health<&OnHealthReport>();
}


// --- snippet: Layer/Requests.md, health answer ---
// The answering side: Counted() keeps the counts of the reply, Reliable below is Counted(), and the stack is HighWater().
using HealthAnswerer = VUB::Stack<+[]() {
	return VUB::Builder()
		.CountDrops()
		.SendQueue(4)
		.Layer<VUB::Requests<HealthCommands, DeviceHandlers>>().Counted()
			.AnswerHealth<HealthCheck::HealthBody, &FillHealth>()
		.Layer<VUB::ReliableOf<40, 4>>().Counted().Retries(3).TimeoutMs(200).Window(4)
		.Transform<XorTransform>()
		.Link<ReportingPacketEnd>()
		.HighWater()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


// The results of the extra tests: the handlers of the two-role layer, of ping, and the counts of the ping and health requests.
inline std::size_t wideReports = 0;
inline ResultStatus lastWideResult = ResultStatus::ok;
inline std::size_t shortReports = 0;
inline ResultStatus lastShortResult = ResultStatus::ok;
inline uint32 lastShortUptime = 0;
inline std::size_t pingDones = 0;
inline ResultStatus lastPingResult = ResultStatus::ok;


void OnHealthWide(const VUB::HealthReport<HealthCheck::HealthWideBody>& report) {
	wideReports++;
	lastWideResult = report.result;
}


void OnHealthShort(const VUB::HealthReport<HealthCheck::HealthShortBody>& report) {
	shortReports++;
	lastShortResult = report.result;
	lastShortUptime = report.user.uptimeS;
}


void OnPingDone(ResultStatus result, uint32) {
	pingDones++;
	lastPingResult = result;
}


// A requester that both asks for a health reply with a body of HealthWideBody (10 bytes) and answers health requests with HealthBody (6
// bytes). Its Reliable is 48 bytes, the largest packet of the layer; it is Counted() with HighWater(), as the answering side needs.
using BothWidePc = VUB::Stack<+[]() {
	return VUB::Builder()
		.CountDrops()
		.SendQueue(4)
		.Layer<VUB::Requests<HealthCommands, PcHandlers>>().Open(4).Tickets(1).Counted()
			.Health<HealthCheck::HealthWideBody>()
			.AnswerHealth<HealthCheck::HealthBody, &FillHealth>()
		.Layer<VUB::ReliableOf<48, 4>>().Counted().Retries(3).TimeoutMs(200).Window(4)
		.Transform<XorTransform>()
		.Link<ReportingPacketEnd>()
		.HighWater()
		.MaxPayload(48)
		.ReceiveQueue(4)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


// The same with a body of HealthShortBody (4 bytes), which is shorter than the answer's body.
using BothShortPc = VUB::Stack<+[]() {
	return VUB::Builder()
		.CountDrops()
		.SendQueue(4)
		.Layer<VUB::Requests<HealthCommands, PcHandlers>>().Open(4).Tickets(1).Counted()
			.Health<HealthCheck::HealthShortBody>()
			.AnswerHealth<HealthCheck::HealthBody, &FillHealth>()
		.Layer<VUB::ReliableOf<48, 4>>().Counted().Retries(3).TimeoutMs(200).Window(4)
		.Transform<XorTransform>()
		.Link<ReportingPacketEnd>()
		.HighWater()
		.MaxPayload(48)
		.ReceiveQueue(4)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


// A requester with ping and health, and a responder with both built-in answers, one reply buffer, and a refusal ring of two.
using PingHealthPc = VUB::Stack<+[]() {
	return VUB::Builder()
		.CountDrops()
		.SendQueue(4)
		.Layer<VUB::Requests<HealthCommands, PcHandlers>>().Open(4).Tickets(1).Ping(16)
			.Health<HealthCheck::HealthBody>()
		.Layer<VUB::ReliableOf<40, 4>>().Retries(3).TimeoutMs(200).Window(4)
		.Transform<XorTransform>()
		.Link<ReportingPacketEnd>()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


using PingHealthDevice = VUB::Stack<+[]() {
	return VUB::Builder()
		.CountDrops()
		.SendQueue(4)
		.Layer<VUB::Requests<HealthCommands, DeviceHandlers>>().Open(1).Tickets(1).Refusals(2).Counted()
			.AnswerPing(16)
			.AnswerHealth<HealthCheck::HealthBody, &FillHealth>()
		.Layer<VUB::ReliableOf<40, 4>>().Counted().Retries(3).TimeoutMs(200).Window(4)
		.Transform<XorTransform>()
		.Link<ReportingPacketEnd>()
		.HighWater()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;
using PingHealthRequests = VUB::Requests<HealthCommands, DeviceHandlers>;


// The same responder with no refusal ring: a built-in or served request that cannot be refused is dropped, and the layer counts it.
using ZeroRefusalDevice = VUB::Stack<+[]() {
	return VUB::Builder()
		.CountDrops()
		.SendQueue(4)
		.Layer<VUB::Requests<HealthCommands, DeviceHandlers>>().Open(1).Tickets(1).Refusals(0).Counted()
			.AnswerPing(16)
			.AnswerHealth<HealthCheck::HealthBody, &FillHealth>()
		.Layer<VUB::ReliableOf<40, 4>>().Counted().Retries(3).TimeoutMs(200).Window(4)
		.Transform<XorTransform>()
		.Link<ReportingPacketEnd>()
		.HighWater()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;
using ZeroRefusalRequests = VUB::Requests<HealthCommands, DeviceHandlers>;


// Commands with a cancellable command, for CancelAll, and a requester with no Reliable below.
using CancelHealthCommands = VUB::CommandSet<+[]() {
	return VUB::Commands()
		.Command<Command::hold>()
			.Request<HealthCheck::HealthProbe>()
			.Reply<HealthCheck::ProbeStatus>()
			.ReplyTimeoutMs(5000)
			.Cancellable();
}>;


using NoReliablePc = VUB::Stack<+[]() {
	return VUB::Builder()
		.CountDrops()
		.SendQueue(4)
		.Layer<VUB::Requests<CancelHealthCommands, VUB::Handlers<>>>().Open(4)
			.Health<HealthCheck::HealthBody>()
		.Link<ReportingPacketEnd>()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


// The session of a requester whose Reliable is 48 bytes (the helper GiveSession finds the 40-byte one only).
template<typename Stack>
void GiveWideSession(Stack& stack) {
	stack.template Find<VUB::ReliableOf<48, 4>>().SetSession(ReliableSession);
}


// A requester whose user body is longer than the answer's: the reply's body is too short for ten bytes, so the report is invalidData.
void TestAskBodyLongerThanAnswer() {
	ResetHealthState();
	wideReports = 0;
	lastWideResult = ResultStatus::ok;
	ReportingPacketLine line(VUB::Faults{});
	BothWidePc pc(line.a);
	HealthDevice device(line.b);
	WireLog log;
	GiveWideSession(pc);
	GiveSession(device);
	Establish(line, pc, device, log, 1000);

	decltype(pc.Requests()) handle = pc.Requests();
	SIM_EXPECT(handle.Health<&OnHealthWide>() == ResultStatus::ok);
	RoundOf(line, pc, device, log, 60000000);
	SIM_EXPECT(healthFills == 1);
	SIM_EXPECT(wideReports == 1);
	SIM_EXPECT(lastWideResult == ResultStatus::invalidData);
}


// A requester whose user body is shorter than the answer's: the first four bytes of the six-byte body are read, the rest is ignored.
void TestAskBodyShorterThanAnswer() {
	ResetHealthState();
	shortReports = 0;
	lastShortResult = ResultStatus::ok;
	lastShortUptime = 0;
	ReportingPacketLine line(VUB::Faults{});
	BothShortPc pc(line.a);
	HealthDevice device(line.b);
	WireLog log;
	GiveWideSession(pc);
	GiveSession(device);
	Establish(line, pc, device, log, 1000);

	decltype(pc.Requests()) handle = pc.Requests();
	SIM_EXPECT(handle.Health<&OnHealthShort>() == ResultStatus::ok);
	RoundOf(line, pc, device, log, 60000000);
	SIM_EXPECT(healthFills == 1);
	SIM_EXPECT(shortReports == 1);
	SIM_EXPECT(lastShortResult == ResultStatus::ok);
	SIM_EXPECT(lastShortUptime == 60);
}


// A ping and a health request in one Process: the responder takes the ping into its one reply buffer, and the health request is refused busy
// from the ring. Both answers reach the requester.
void TestPingAndHealthInOneWalk() {
	ResetHealthState();
	pingDones = 0;
	lastPingResult = ResultStatus::ok;
	ReportingPacketLine line(VUB::Faults{});
	PingHealthPc pc(line.a);
	PingHealthDevice device(line.b);
	WireLog log;
	GiveSession(pc);
	GiveSession(device);
	Establish(line, pc, device, log, 1000);

	decltype(pc.Requests()) handle = pc.Requests();
	SIM_EXPECT(handle.Ping<&OnPingDone>(0) == ResultStatus::ok);
	SIM_EXPECT(handle.Health<&OnHealth>() == ResultStatus::ok);
	RoundOf(line, pc, device, log, 60000000);
	SIM_EXPECT(pingDones == 1);
	SIM_EXPECT(lastPingResult == ResultStatus::ok);
	SIM_EXPECT(healthBusies == 1);
	SIM_EXPECT(healthFills == 0);
	SIM_EXPECT(device.Find<PingHealthRequests>().Refused() == 0);
}


// A built-in request refused with no room in the ring is counted in refused, as a served request is. The ping takes the reply buffer; the
// health request is refused busy, which the ring cannot hold, so the layer counts it and sends nothing. The request ends by its deadline.
void TestBuiltInRefusalCountedWhenRingFull() {
	ResetHealthState();
	pingDones = 0;
	lastPingResult = ResultStatus::ok;
	ReportingPacketLine line(VUB::Faults{});
	PingHealthPc pc(line.a);
	ZeroRefusalDevice device(line.b);
	WireLog log;
	GiveSession(pc);
	GiveSession(device);
	Establish(line, pc, device, log, 1000);

	decltype(pc.Requests()) handle = pc.Requests();
	SIM_EXPECT(handle.Ping<&OnPingDone>(0) == ResultStatus::ok);
	SIM_EXPECT(handle.Health<&OnHealth>() == ResultStatus::ok);
	RoundOf(line, pc, device, log, 60000000);
	SIM_EXPECT(pingDones == 1);
	SIM_EXPECT(lastPingResult == ResultStatus::ok);
	SIM_EXPECT(healthFills == 0);
	SIM_EXPECT(device.Find<ZeroRefusalRequests>().Refused() == 1);
	SIM_EXPECT(healthReports == 0);

	// The dropped request gets no reply: its deadline (1000 ms) ends it with timeout.
	RoundOf(line, pc, device, log, 61500000);
	SIM_EXPECT(healthReports == 1);
	SIM_EXPECT(lastHealth.result == ResultStatus::timeout);
}


// The same count for a served command: two unknown commands in one walk of a responder with no refusal ring are both refused, and counted.
void TestServedRefusalCountedWhenRingFull() {
	ResetHealthState();
	ReportingPacketLine line(VUB::Faults{});
	PingHealthPc pc(line.a);
	ZeroRefusalDevice device(line.b);
	WireLog log;
	GiveSession(pc);
	GiveSession(device);
	Establish(line, pc, device, log, 1000);

	decltype(pc.Requests()) handle = pc.Requests();
	SIM_EXPECT(SendUnknown(handle));
	SIM_EXPECT(SendUnknown(handle));
	RoundOf(line, pc, device, log, 60000000);
	SIM_EXPECT(device.Find<ZeroRefusalRequests>().Refused() == 2);
}


// CancelAll on a requester with no Reliable: the cancellable request gets its CANCEL at once, and the health request is not cancelled. The
// health request ends with timeout at its own deadline; the cancelled request ends with operationCancelled at its deadline.
void TestCancelAllKeepsHealthOpen() {
	ResetHealthState();
	holdFinals = 0;
	lastHoldResult = ResultStatus::ok;
	ReportingPacketLine line(VUB::Faults{});
	NoReliablePc pc(line.a);
	decltype(pc.Requests()) handle = pc.Requests();
	handle.SetSession(1);

	testNowUs = 1000000;
	SIM_EXPECT(SendHold(handle));
	SIM_EXPECT(handle.Health<&OnHealth>() == ResultStatus::ok);
	pc.Process();
	SIM_EXPECT(handle.CancelAll() == ResultStatus::ok);

	testNowUs = 2100000;
	pc.Process();
	SIM_EXPECT(healthReports == 1);
	SIM_EXPECT(lastHealth.result == ResultStatus::timeout);
	SIM_EXPECT(holdFinals == 0);

	testNowUs = 6100000;
	pc.Process();
	SIM_EXPECT(holdFinals == 1);
	SIM_EXPECT(lastHoldResult == ResultStatus::operationCancelled);
}


// A peer restart with a health request open: the requester's Reliable sees a session of the responder with another boot value, and closes the
// open health request with reset (the request is not answered by the old responder).
void TestRestartClosesHealth() {
	ResetHealthState();
	ReportingPacketLine line(VUB::Faults{});
	HealthPc pc(line.a);
	std::optional<HealthDevice> deviceBox;
	deviceBox.emplace(line.b);
	WireLog log;
	GiveSession(pc);
	GiveSession(*deviceBox);
	Establish(line, pc, *deviceBox, log, 1000);

	HealthPcHandle handle = pc.Requests();
	SIM_EXPECT(handle.Health<&OnHealth>() == ResultStatus::ok);
	testNowUs = 60000000;
	pc.Process();
	DropSink dropped;
	line.DeliverToB(dropped);

	// The responder restarts: a second object with another boot value. Its first send starts a session that the requester sees as a restart.
	deviceBox.reset();
	deviceBox.emplace(line.b);
	HealthDevice& fresh = *deviceBox;
	GiveSession(fresh, 0x3456);
	HealthDeviceHandle freshHandle = fresh.Requests();
	SIM_EXPECT(UnknownError(freshHandle) == ResultStatus::busy);
	Settle(line, pc, fresh, log, 60050000, 4);
	SIM_EXPECT(healthReports == 1);
	SIM_EXPECT(lastHealth.result == ResultStatus::reset);
}


int main() {
	TestZeroCountersReplyBytes();
	TestReplyWithoutUserBody();
	TestResponderWithoutHealthIsNotSupported();
	TestUnknownCommandIdIsNotSupported();
	TestSecondBuiltInIsBusy();
	TestShortReplyIsInvalidData();
	TestHealthDoesNotTakeTicket();
	TestFillRunsOnceFromProcess();
	TestCountersInReplyAfterRealEvents();
	TestCountersSaturate();
	TestAskBodyLongerThanAnswer();
	TestAskBodyShorterThanAnswer();
	TestPingAndHealthInOneWalk();
	TestBuiltInRefusalCountedWhenRingFull();
	TestServedRefusalCountedWhenRingFull();
	TestCancelAllKeepsHealthOpen();
	TestRestartClosesHealth();
	return SimFinish("TestVubRequestsHealth");
}
