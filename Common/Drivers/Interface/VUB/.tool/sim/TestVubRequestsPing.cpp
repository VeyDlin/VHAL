#include "Chains.h"
#include "ReliableChains.h"
#include "RequestsMotor.h"
#include "SimCheck.h"
#include <Drivers/Interface/VUB/Requests.h>
#include <Utilities/DataTypes.h>
#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <utility>

// Ping of the requests layer over a Reliable link: two stacks on a reporting packet line, as TestVubRequests.cpp. The expected bytes
// are the wire of the ping (the request and the reply). The request goes out in the next Process and is stamped with that Process's
// clock; its deadline is BuiltInTimeoutMs, 1000 ms by default; the padding is the low byte of each index. The clock is in microseconds:
// 1 ms is 1000. The requester does not compare the echoed padding, so the tests check the bytes on the wire. Each test starts from zero.


// The command ids of these tests. The value is the id on the wire.
enum class Command : uint8 {
	start = 0x12,
	stop = 0x13
};


// The commands of these tests: start (with a ticket) and stop (answered at once). Ping (0xF0) is built in and is not listed.
using PingCommands = VUB::CommandSet<+[]() {
	return VUB::Commands()
		.Command<Command::start>()
			.Request<Motor::MotorStart>()
			.Reply<Motor::MotorStatus, Motor::MotorReport>()
			.ReplyTimeoutMs(5000)
			.TicketTimeoutMs(60000)
		.Command<Command::stop>()
			.Reply<Motor::MotorStatus>()
			.ReplyTimeoutMs(2000)
			.TicketTimeoutMs(2000);
}>;


using StartTicket = VUB::Ticket<PingCommands, Command::start>;
using StopTicket = VUB::Ticket<PingCommands, Command::stop>;


// The responder's state: the start ticket it holds, and the stop it answers at once.
inline StartTicket heldStart;
inline bool startHeld = false;


// The requester's state: the start's final, and each ping's result and round trip, in the order the handler ran.
inline std::size_t startFinals = 0;
inline ResultStatus startResult = ResultStatus::ok;
inline std::size_t pingCalls = 0;
inline std::array<ResultStatus, 16> pingResults{};
inline std::array<uint32, 16> pingTrips{};

// The far peer's ping, in the test where both sides ping at once.
inline std::size_t farPingCalls = 0;
inline ResultStatus farPingResult = ResultStatus::ok;
inline uint32 farPingTripUs = 0;


void OnStart(StartTicket ticket, const Motor::MotorStart&) {
	heldStart = ticket;
	startHeld = true;
}


void OnStop(StopTicket ticket) {
	ticket.Finish(Motor::MotorStatus::started);
}


void OnStartFinal(const VUB::Final<Motor::MotorStatus, Motor::MotorReport>& reply) {
	startFinals++;
	startResult = reply.result;
}


void OnPing(ResultStatus result, uint32 roundTripUs) {
	if (pingCalls < pingResults.size()) {
		pingResults[pingCalls] = result;
		pingTrips[pingCalls] = roundTripUs;
	}

	pingCalls++;
}


// The handler of the ping that the far peer sends in the both-sides test: its own counters, so the two handlers can be told apart.
void OnPingFar(ResultStatus result, uint32 roundTripUs) {
	farPingCalls++;
	farPingResult = result;
	farPingTripUs = roundTripUs;
}


using PingHandlers = VUB::Handlers<
	VUB::On<Command::start, &OnStart>,
	VUB::On<Command::stop, &OnStop>>;


// The requester: four slots and ping up to 16 bytes of padding. The built-in deadline is the default, 1000 ms.
using PingRequester = VUB::Stack<+[]() {
	return VUB::Builder()
		.Layer<VUB::Requests<PingCommands, VUB::Handlers<>>>().Open(4)
			.Ping(16)
		.Layer<VUB::ReliableOf<40, 4>>().Retries(3).TimeoutMs(200).Window(4)
		.Link<ReportingPacketEnd>()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


// The requester with a built-in deadline of 300 ms (BuiltInTimeoutMs).
using PingShortRequester = VUB::Stack<+[]() {
	return VUB::Builder()
		.Layer<VUB::Requests<PingCommands, VUB::Handlers<>>>().Open(4)
			.Ping(16)
			.BuiltInTimeoutMs(300)
		.Layer<VUB::ReliableOf<40, 4>>().Retries(3).TimeoutMs(200).Window(4)
		.Link<ReportingPacketEnd>()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


// The responder: two tickets and ping up to 16 bytes of padding.
using PingResponder = VUB::Stack<+[]() {
	return VUB::Builder()
		.Layer<VUB::Requests<PingCommands, PingHandlers>>().Tickets(2)
			.AnswerPing(16)
		.Layer<VUB::ReliableOf<40, 4>>().Retries(3).TimeoutMs(200).Window(4)
		.Link<ReportingPacketEnd>()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


// The responder with one ticket, which the start of one test holds.
using PingOneTicketResponder = VUB::Stack<+[]() {
	return VUB::Builder()
		.Layer<VUB::Requests<PingCommands, PingHandlers>>().Tickets(1)
			.AnswerPing(16)
		.Layer<VUB::ReliableOf<40, 4>>().Retries(3).TimeoutMs(200).Window(4)
		.Link<ReportingPacketEnd>()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


// A responder built without ping: a ping to it is an unknown command.
using PingSilentResponder = VUB::Stack<+[]() {
	return VUB::Builder()
		.Layer<VUB::Requests<PingCommands, PingHandlers>>().Tickets(2)
		.Layer<VUB::ReliableOf<40, 4>>().Retries(3).TimeoutMs(200).Window(4)
		.Link<ReportingPacketEnd>()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


// A stack that both requests and responds: two open slots, two tickets, and ping.
using PingPeer = VUB::Stack<+[]() {
	return VUB::Builder()
		.Layer<VUB::Requests<PingCommands, PingHandlers>>().Open(2).Tickets(2)
			.Ping(16)
			.AnswerPing(16)
		.Layer<VUB::ReliableOf<40, 4>>().Retries(3).TimeoutMs(200).Window(4)
		.Link<ReportingPacketEnd>()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


// The requester with a built-in deadline of 5 s, longer than Reliable's give-up.
using PingLongRequester = VUB::Stack<+[]() {
	return VUB::Builder()
		.Layer<VUB::Requests<PingCommands, VUB::Handlers<>>>().Open(4)
			.Ping(16)
			.BuiltInTimeoutMs(5000)
		.Layer<VUB::ReliableOf<40, 4>>().Retries(3).TimeoutMs(200).Window(4)
		.Link<ReportingPacketEnd>()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


// The requester and the responder with no Reliable below: a link directly under the layer.
using PlainPingRequester = VUB::Stack<+[]() {
	return VUB::Builder()
		.Layer<VUB::Requests<PingCommands, VUB::Handlers<>>>().Open(4)
			.Ping(16)
		.Link<ReportingPacketEnd>()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


using PlainPingResponder = VUB::Stack<+[]() {
	return VUB::Builder()
		.Layer<VUB::Requests<PingCommands, PingHandlers>>().Tickets(2)
			.AnswerPing(16)
		.Link<ReportingPacketEnd>()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


// A side that answers pings and serves nothing: no command handler and no ticket, with the default refusal ring.
using PingAnswerOnlyResponder = VUB::Stack<+[]() {
	return VUB::Builder()
		.Layer<VUB::Requests<PingCommands, VUB::Handlers<>>>()
			.AnswerPing(16)
		.Layer<VUB::ReliableOf<40, 4>>().Retries(3).TimeoutMs(200).Window(4)
		.Link<ReportingPacketEnd>()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


// The same side with Refusals(0): a second ping while its reply buffer is unsent is dropped, with no refusal.
using PingDroppingResponder = VUB::Stack<+[]() {
	return VUB::Builder()
		.Layer<VUB::Requests<PingCommands, VUB::Handlers<>>>()
			.Refusals(0)
			.AnswerPing(16)
		.Layer<VUB::ReliableOf<40, 4>>().Retries(3).TimeoutMs(200).Window(4)
		.Link<ReportingPacketEnd>()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


using PingHandle = decltype(std::declval<PingRequester&>().Requests());


// Sends the packets of one direction to the stack on the far end and keeps a copy of each. cut bytes are taken off the end of the first
// packet of 13 bytes or more (the reply, with its Reliable header), and the cut is then spent.
template<typename Stack>
struct Tee {
	ReliableCapture* capture;
	Stack* target;
	std::size_t cut;


	void Feed(uint8) {
	}


	void FeedPacket(std::span<const uint8> packet) {
		std::size_t length = packet.size();
		if (cut > 0 && length >= 13) {
			length -= cut;
			cut = 0;
		}

		std::span<const uint8> kept(packet.data(), length);
		capture->FeedPacket(kept);
		target->FeedPacket(kept);
	}
};


// The packets of one round, by direction: toFar carries the requester's packets, toNear the responder's.
struct WireLog {
	ReliableCapture toFar;
	ReliableCapture toNear;


	void Clear() {
		toFar = ReliableCapture{};
		toNear = ReliableCapture{};
	}
};


// One round: the near stack runs, its packets reach the far stack, the far stack runs, its packets reach the near stack (cut by cutReply
// bytes), and the near stack runs again.
template<typename NearStack, typename FarStack>
void Round(ReportingPacketLine& line, NearStack& near, FarStack& far, WireLog& log, uint32 nowUs, std::size_t cutReply = 0) {
	testNowUs = nowUs;
	log.Clear();
	near.Process();
	Tee<FarStack> forward{&log.toFar, &far, 0};
	line.DeliverToB(forward);
	far.Process();
	Tee<NearStack> back{&log.toNear, &near, cutReply};
	line.DeliverToA(back);
	near.Process();
}


// The sessions of both ends, as TestVubRequests.cpp starts them: each end's sending session opens with one round, and the responder's
// first send is busy until then.
template<typename NearStack, typename FarStack>
void Establish(ReportingPacketLine& line, NearStack& near, FarStack& far, WireLog& log) {
	GiveSession(near);
	GiveSession(far);
	// Reliable starts a session with its first send: that send is busy, and the sync goes out in the next Process. Each end sends one trigger.
	std::array<uint8, 1> trigger = {0xEE};
	SIM_EXPECT(near.template LayerOutbox<0>().Send(std::span<const uint8>(trigger.data(), trigger.size())) == ResultStatus::busy);
	SIM_EXPECT(far.template LayerOutbox<0>().Send(std::span<const uint8>(trigger.data(), trigger.size())) == ResultStatus::busy);
	Round(line, near, far, log, 50);
	for (uint32 round = 0; round < 3; round++) {
		Round(line, near, far, log, 100 + round);
	}
}


// Clears the state of the ping tests.
void ResetPingState() {
	ResetTestState();
	ResetReliableState();
	startHeld = false;
	startFinals = 0;
	startResult = ResultStatus::ok;
	pingCalls = 0;
	pingResults = {};
	pingTrips = {};
	farPingCalls = 0;
	farPingResult = ResultStatus::ok;
	farPingTripUs = 0;
}


// A ping from the stack, called with the clock at nowUs. The request is sent by the next Process, which stamps it with its own clock.
// Returns the call's result.
template<typename Stack>
ResultStatus SendPing(Stack& stack, uint32 nowUs, uint16 padding) {
	testNowUs = nowUs;
	decltype(stack.Requests()) handle = stack.Requests();
	return handle.template Ping<&OnPing>(padding);
}


// The same ping, with the far handler: its result is kept apart from the near one's.
template<typename Stack>
ResultStatus SendPingFar(Stack& stack, uint32 nowUs, uint16 padding) {
	testNowUs = nowUs;
	decltype(stack.Requests()) handle = stack.Requests();
	return handle.template Ping<&OnPingFar>(padding);
}


// A start request from the requester, with its final recorded: true when the request was accepted.
bool SendStart(PingRequester& near) {
	PingHandle handle = near.Requests();
	auto sent = handle.Send<Command::start, &OnStartFinal>(Motor::MotorStart{});
	return sent.IsErr() == false;
}


// The packet of the capture, as the bytes it holds.
std::span<const uint8> PacketOf(const ReliableCapture& capture, std::size_t index) {
	return std::span<const uint8>(capture.packets[index].data(), capture.lengths[index]);
}


// The bytes as a span.
template<std::size_t Size>
std::span<const uint8> View(const std::array<uint8, Size>& bytes) {
	return std::span<const uint8>(bytes.data(), bytes.size());
}


// True when the packet holds the bytes, the last of them fromEnd bytes short of its end.
bool BytesAt(std::span<const uint8> packet, std::size_t fromEnd, std::span<const uint8> bytes) {
	if (packet.size() < fromEnd + bytes.size()) {
		return false;
	}

	std::size_t start = packet.size() - fromEnd - bytes.size();
	for (std::size_t index = 0; index < bytes.size(); index++) {
		if (packet[start + index] != bytes[index]) {
			return false;
		}
	}

	return true;
}


// The index of the first packet of the capture that holds the bytes, or the capture's size when none does.
std::size_t FindPacket(const ReliableCapture& capture, std::size_t fromEnd, std::span<const uint8> bytes) {
	for (std::size_t index = 0; index < capture.count && index < capture.packets.size(); index++) {
		if (BytesAt(PacketOf(capture, index), fromEnd, bytes)) {
			return index;
		}
	}

	return capture.packets.size();
}


// The bytes of a ping and its reply, checked on the line. Eight pings go first, so the ninth takes number 9.
void TestPingBytesOnTheWire() {
	ResetPingState();
	ReportingPacketLine line(VUB::Faults{});
	PingRequester near(line.a);
	PingResponder far(line.b);
	WireLog log;
	Establish(line, near, far, log);

	for (uint32 index = 0; index < 8; index++) {
		SIM_EXPECT(SendPing(near, 1000 + index * 10, 0) == ResultStatus::ok);
		Round(line, near, far, log, 1000 + index * 10);
		Round(line, near, far, log, 1000 + index * 10);
	}

	SIM_EXPECT(pingCalls == 8);

	// Ping, number 9, stamp 0x00012345 (the clock of the Process that sends it), no padding; its reply carries the stamp back.
	SIM_EXPECT(SendPing(near, 0x00012345, 0) == ResultStatus::ok);
	Round(line, near, far, log, 0x00012345);
	std::array<uint8, 9> request = {0x01, 0xF0, 0x09, 0x00, 0x09, 0x45, 0x23, 0x01, 0x00};
	std::array<uint8, 10> reply = {0x03, 0x09, 0x00, 0x09, 0x00, 0x00, 0x45, 0x23, 0x01, 0x00};
	SIM_EXPECT(FindPacket(log.toFar, 0, View(request)) < log.toFar.packets.size());
	SIM_EXPECT(FindPacket(log.toNear, 0, View(reply)) < log.toNear.packets.size());
	SIM_EXPECT(pingCalls == 9);
	SIM_EXPECT(pingResults[8] == ResultStatus::ok);
	SIM_EXPECT(pingTrips[8] == 0);
}


// A ping with five bytes of padding: the request and the reply keep the header, and both carry the padding, which is the low byte of
// each index: 00 01 02 03 04.
void TestPingPaddingIsEchoedInTheReply() {
	ResetPingState();
	ReportingPacketLine line(VUB::Faults{});
	PingRequester near(line.a);
	PingResponder far(line.b);
	WireLog log;
	Establish(line, near, far, log);

	// The first ping of the test takes number 1 and tag 1.
	std::array<uint8, 14> request = {0x01, 0xF0, 0x01, 0x00, 0x01, 0x45, 0x23, 0x01, 0x00, 0x00, 0x01, 0x02, 0x03, 0x04};
	std::array<uint8, 15> reply = {0x03, 0x01, 0x00, 0x01, 0x00, 0x00, 0x45, 0x23, 0x01, 0x00, 0x00, 0x01, 0x02, 0x03, 0x04};
	SIM_EXPECT(SendPing(near, 0x00012345, 5) == ResultStatus::ok);
	Round(line, near, far, log, 0x00012345);
	SIM_EXPECT(FindPacket(log.toFar, 0, View(request)) < log.toFar.packets.size());
	SIM_EXPECT(FindPacket(log.toNear, 0, View(reply)) < log.toNear.packets.size());
	SIM_EXPECT(pingCalls == 1);
	SIM_EXPECT(pingResults[0] == ResultStatus::ok);
}


// A padding of 17 bytes is above the maximum of 16 that the sender declares: it is refused at once, and nothing is sent.
void TestPingPaddingAboveMaximumIsRefused() {
	ResetPingState();
	ReportingPacketLine line(VUB::Faults{});
	PingRequester near(line.a);
	PingResponder far(line.b);
	WireLog log;
	Establish(line, near, far, log);

	SIM_EXPECT(SendPing(near, 1000, 17) == ResultStatus::messageTooLong);
	Round(line, near, far, log, 1000);
	SIM_EXPECT(log.toFar.count == 0);
	SIM_EXPECT(pingCalls == 0);

	// The maximum itself is accepted. The refused ping used no number, so this one is number 1; its stamp is 1100 (0x44C) and its
	// padding is 00 to 0F.
	std::array<uint8, 25> request = {0x01, 0xF0, 0x01, 0x00, 0x01, 0x4C, 0x04, 0x00, 0x00,
		0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F};
	SIM_EXPECT(SendPing(near, 1100, 16) == ResultStatus::ok);
	Round(line, near, far, log, 1100);
	SIM_EXPECT(FindPacket(log.toFar, 0, View(request)) < log.toFar.packets.size());
	Round(line, near, far, log, 1100);
	SIM_EXPECT(pingCalls == 1);
	SIM_EXPECT(pingResults[0] == ResultStatus::ok);
}


// The request is stamped by the Process that sends it, not by the call: the ping is called at 1000 and sent at 1500, so the stamp is 1500
// (0x5DC), the reply echoes it, and the round trip is 0.
void TestPingStampIsTheClockOfTheProcessThatSendsIt() {
	ResetPingState();
	ReportingPacketLine line(VUB::Faults{});
	PingRequester near(line.a);
	PingResponder far(line.b);
	WireLog log;
	Establish(line, near, far, log);

	std::array<uint8, 9> request = {0x01, 0xF0, 0x01, 0x00, 0x01, 0xDC, 0x05, 0x00, 0x00};
	std::array<uint8, 10> reply = {0x03, 0x01, 0x00, 0x01, 0x00, 0x00, 0xDC, 0x05, 0x00, 0x00};
	SIM_EXPECT(SendPing(near, 1000, 0) == ResultStatus::ok);
	Round(line, near, far, log, 1500);
	SIM_EXPECT(FindPacket(log.toFar, 0, View(request)) < log.toFar.packets.size());
	SIM_EXPECT(FindPacket(log.toNear, 0, View(reply)) < log.toNear.packets.size());
	SIM_EXPECT(pingCalls == 1);
	SIM_EXPECT(pingResults[0] == ResultStatus::ok);
	SIM_EXPECT(pingTrips[0] == 0);
}


// A ping sent at sendUs whose reply is taken at replyUs: the request goes out in the near stack's round at sendUs, and the reply waits in
// the line until replyUs.
void PingSplit(ReportingPacketLine& line, PingRequester& near, PingResponder& far, WireLog& log, uint32 sendUs, uint32 replyUs) {
	log.Clear();
	SIM_EXPECT(SendPing(near, sendUs, 0) == ResultStatus::ok);
	testNowUs = sendUs;
	near.Process();
	Tee<PingResponder> forward{&log.toFar, &far, 0};
	line.DeliverToB(forward);
	far.Process();
	testNowUs = replyUs;
	Tee<PingRequester> back{&log.toNear, &near, 0};
	line.DeliverToA(back);
	near.Process();
}


// The round trip is the requester's clock minus the stamp, computed on the requester's clock: 500 us here, and 512 us across the wrap of the 32-bit clock.
void TestPingRoundTripFromEchoedStamp() {
	ResetPingState();
	ReportingPacketLine line(VUB::Faults{});
	PingRequester near(line.a);
	PingResponder far(line.b);
	WireLog log;
	Establish(line, near, far, log);

	PingSplit(line, near, far, log, 1000, 1500);
	SIM_EXPECT(pingCalls == 1);
	SIM_EXPECT(pingResults[0] == ResultStatus::ok);
	SIM_EXPECT(pingTrips[0] == 500);

	// Sent at 0xFFFFFF00 and answered at 0x00000100: the clock wraps, and the difference is 0x200.
	PingSplit(line, near, far, log, 0xFFFFFF00, 0x00000100);
	SIM_EXPECT(pingCalls == 2);
	SIM_EXPECT(pingResults[1] == ResultStatus::ok);
	SIM_EXPECT(pingTrips[1] == 512);
}


// A peer built without ping cannot serve the ping request: the requester gets notSupported.
void TestPingToPeerWithoutPingIsNotSupported() {
	ResetPingState();
	ReportingPacketLine line(VUB::Faults{});
	PingRequester near(line.a);
	PingSilentResponder far(line.b);
	WireLog log;
	Establish(line, near, far, log);

	SIM_EXPECT(SendPing(near, 1000, 0) == ResultStatus::ok);
	Round(line, near, far, log, 1000);
	Round(line, near, far, log, 1000);
	SIM_EXPECT(pingCalls == 1);
	SIM_EXPECT(pingResults[0] == ResultStatus::notSupported);
}


// Every packet of the requester is lost: Reliable resends and then gives up, and the ping ends with retryExhausted, once. Reliable gives
// up at about 800 ms, ahead of the 1000 ms default deadline.
void TestPingLostIsRetryExhausted() {
	ResetPingState();
	ReportingPacketLine line(VUB::Faults{});
	PingRequester near(line.a);
	PingResponder far(line.b);
	WireLog log;
	Establish(line, near, far, log);

	SIM_EXPECT(SendPing(near, 1000, 0) == ResultStatus::ok);
	uint32 now = 1000;
	for (uint32 step = 0; step < 25; step++) {
		testNowUs = now;
		near.Process();
		now += 100000;
	}

	SIM_EXPECT(pingCalls == 1);
	SIM_EXPECT(pingResults[0] == ResultStatus::retryExhausted);
	testNowUs = now;
	near.Process();
	SIM_EXPECT(pingCalls == 1);
}


// No reply comes: the ping ends by its deadline. BuiltInTimeoutMs is the default, 1000 ms, so the deadline is 1 001 000 us (the stamp
// at 1000 plus 1 000 000). It is still open at 301 001 us, and it times out after the deadline.
void TestPingTimesOutAtDefaultDeadline() {
	ResetPingState();
	ReportingPacketLine line(VUB::Faults{});
	PingRequester near(line.a);
	PingResponder far(line.b);
	WireLog log;
	Establish(line, near, far, log);

	SIM_EXPECT(SendPing(near, 1000, 0) == ResultStatus::ok);
	testNowUs = 1000;
	near.Process();
	testNowUs = 301001;
	near.Process();
	SIM_EXPECT(pingCalls == 0);
	testNowUs = 1001001;
	near.Process();
	near.Process();
	SIM_EXPECT(pingCalls == 1);
	SIM_EXPECT(pingResults[0] == ResultStatus::timeout);
}


// The requester's BuiltInTimeoutMs(300) sets the deadline: the stamp at 1000 plus 300 000 us, so 301 000 us. The ping is open at 300 999
// and times out after 301 000.
void TestPingBuiltInTimeoutOfTheStackIsUsed() {
	ResetPingState();
	ReportingPacketLine line(VUB::Faults{});
	PingShortRequester near(line.a);
	PingResponder far(line.b);
	WireLog log;
	Establish(line, near, far, log);

	SIM_EXPECT(SendPing(near, 1000, 0) == ResultStatus::ok);
	testNowUs = 1000;
	near.Process();
	testNowUs = 300999;
	near.Process();
	SIM_EXPECT(pingCalls == 0);
	testNowUs = 301001;
	near.Process();
	near.Process();
	SIM_EXPECT(pingCalls == 1);
	SIM_EXPECT(pingResults[0] == ResultStatus::timeout);
}


// The final of a ping that is code 0 without its status byte is malformed: the layer drops it (counted in layerError) and the request it
// names stays open. The ping then ends by its deadline. The reply is cut to five bytes: its Reliable header and 03 01 00 01 00.
void TestPingFinalWithoutStatusIsDroppedAndEndsAtDeadline() {
	ResetPingState();
	ReportingPacketLine line(VUB::Faults{});
	PingRequester near(line.a);
	PingResponder far(line.b);
	WireLog log;
	Establish(line, near, far, log);

	SIM_EXPECT(SendPing(near, 1000, 0) == ResultStatus::ok);
	Round(line, near, far, log, 1000, 5);
	SIM_EXPECT(pingCalls == 0);
	Round(line, near, far, log, 1000999);
	SIM_EXPECT(pingCalls == 0);
	Round(line, near, far, log, 1001001);
	SIM_EXPECT(pingCalls == 1);
	SIM_EXPECT(pingResults[0] == ResultStatus::timeout);
}


// A reply whose stamp part is cut short is a bad body (a required field is missing), and the handler gets invalidData.
void TestPingReplyWithShortStampIsInvalidData() {
	ResetPingState();
	ReportingPacketLine line(VUB::Faults{});
	PingRequester near(line.a);
	PingResponder far(line.b);
	WireLog log;
	Establish(line, near, far, log);

	// Two bytes off the reply: the stamp has two of its four bytes.
	SIM_EXPECT(SendPing(near, 1000, 0) == ResultStatus::ok);
	Round(line, near, far, log, 1000, 2);
	Round(line, near, far, log, 1000);
	SIM_EXPECT(pingCalls == 1);
	SIM_EXPECT(pingResults[0] == ResultStatus::invalidData);
}


// Two pings in one burst: the responder takes the first into its one built-in reply buffer in Take, and the second finds the buffer still
// unsent, so it is refused busy from the refusal ring. The order of the two answers is not fixed, so either order passes.
void TestPingSecondWhileReplyIsUnsentIsBusy() {
	ResetPingState();
	ReportingPacketLine line(VUB::Faults{});
	PingRequester near(line.a);
	PingResponder far(line.b);
	WireLog log;
	Establish(line, near, far, log);

	SIM_EXPECT(SendPing(near, 1000, 0) == ResultStatus::ok);
	SIM_EXPECT(SendPing(near, 1000, 0) == ResultStatus::ok);
	Round(line, near, far, log, 1000);
	SIM_EXPECT(pingCalls == 2);
	bool firstOkThenBusy = pingResults[0] == ResultStatus::ok && pingResults[1] == ResultStatus::busy;
	bool busyThenFirstOk = pingResults[0] == ResultStatus::busy && pingResults[1] == ResultStatus::ok;
	SIM_EXPECT(firstOkThenBusy || busyThenFirstOk);

	// The buffer is free once its reply has gone: a ping sent after that is answered.
	SIM_EXPECT(SendPing(near, 2000, 0) == ResultStatus::ok);
	Round(line, near, far, log, 2000);
	Round(line, near, far, log, 2000);
	SIM_EXPECT(pingCalls == 3);
	SIM_EXPECT(pingResults[2] == ResultStatus::ok);
}


// Four slots (Open(4)) hold four pings, and a fifth ping is busy at once: the call returns busy and nothing is counted for it.
void TestPingWithoutFreeSlotIsBusy() {
	ResetPingState();
	ReportingPacketLine line(VUB::Faults{});
	PingRequester near(line.a);
	PingResponder far(line.b);
	WireLog log;
	Establish(line, near, far, log);

	for (uint32 index = 0; index < 4; index++) {
		SIM_EXPECT(SendPing(near, 1000, 0) == ResultStatus::ok);
	}

	SIM_EXPECT(SendPing(near, 1000, 0) == ResultStatus::busy);
	SIM_EXPECT(pingCalls == 0);
}


// Both stacks ping in one round: each answers the other's request, and each ping's own handler gets its reply once.
void TestPingFromBothSidesAtOnce() {
	ResetPingState();
	ReportingPacketLine line(VUB::Faults{});
	PingPeer near(line.a);
	PingPeer far(line.b);
	WireLog log;
	Establish(line, near, far, log);

	SIM_EXPECT(SendPing(near, 1000, 0) == ResultStatus::ok);
	SIM_EXPECT(SendPingFar(far, 1000, 0) == ResultStatus::ok);
	Round(line, near, far, log, 1000);
	Round(line, near, far, log, 1000);
	SIM_EXPECT(pingCalls == 1);
	SIM_EXPECT(pingResults[0] == ResultStatus::ok);
	SIM_EXPECT(farPingCalls == 1);
	SIM_EXPECT(farPingResult == ResultStatus::ok);
}


// The responder's only ticket is held by a start request. A ping is answered anyway, takes no ticket, and the start is still in flight: its
// ticket stays open until the device finishes it, and then its final reaches the requester.
void TestPingUsesNoTicketAndKeepsStartInFlight() {
	ResetPingState();
	ReportingPacketLine line(VUB::Faults{});
	PingRequester near(line.a);
	PingOneTicketResponder far(line.b);
	WireLog log;
	Establish(line, near, far, log);

	SIM_EXPECT(SendStart(near));
	Round(line, near, far, log, 1000);
	Round(line, near, far, log, 1000);
	SIM_EXPECT(startHeld);

	SIM_EXPECT(SendPing(near, 1100, 0) == ResultStatus::ok);
	Round(line, near, far, log, 1100);
	Round(line, near, far, log, 1100);
	SIM_EXPECT(pingCalls == 1);
	SIM_EXPECT(pingResults[0] == ResultStatus::ok);
	SIM_EXPECT(heldStart.Open());
	SIM_EXPECT(startFinals == 0);

	Motor::MotorReport report{};
	report.speedRpm = 1200;
	report.currentMa = 850;
	SIM_EXPECT(heldStart.Finish(Motor::MotorStatus::started, report) == ResultStatus::ok);
	Round(line, near, far, log, 1200);
	Round(line, near, far, log, 1200);
	SIM_EXPECT(startFinals == 1);
	SIM_EXPECT(startResult == ResultStatus::ok);
}


// The stack of the handler test: the requester whose ping handler sends a second ping, and that ping's handler.
inline PingRequester* chainNear = nullptr;
inline bool chainSent = false;
inline ResultStatus chainStatus = ResultStatus::notAvailable;
inline std::size_t secondCalls = 0;
inline ResultStatus secondResult = ResultStatus::notAvailable;


void OnPingSecond(ResultStatus result, uint32) {
	secondCalls++;
	secondResult = result;
}


// A ping handler that sends the second ping from inside its own call. The slot of the first ping is freed, and then the handler runs.
void OnPingChained(ResultStatus result, uint32 roundTripUs) {
	OnPing(result, roundTripUs);
	if (!chainSent) {
		chainSent = true;
		PingHandle handle = chainNear->Requests();
		chainStatus = handle.template Ping<&OnPingSecond>(0);
	}
}


// A ping whose reply comes after its deadline: the deadline ends the ping with timeout, and the late reply is dropped.
void TestPingLateReplyIsDroppedAfterDeadline() {
	ResetPingState();
	ReportingPacketLine line(VUB::Faults{});
	PingRequester near(line.a);
	PingResponder far(line.b);
	WireLog log;
	Establish(line, near, far, log);

	SIM_EXPECT(SendPing(near, 1000, 0) == ResultStatus::ok);
	testNowUs = 1000;
	near.Process();
	Tee<PingResponder> forward{&log.toFar, &far, 0};
	line.DeliverToB(forward);
	far.Process();
	testNowUs = 1001001;
	near.Process();
	SIM_EXPECT(pingCalls == 1);
	SIM_EXPECT(pingResults[0] == ResultStatus::timeout);
	Tee<PingRequester> back{&log.toNear, &near, 0};
	line.DeliverToA(back);
	near.Process();
	SIM_EXPECT(pingCalls == 1);
}


// A ping is open when the responder restarts: the old boot takes the request and restarts without answering, so the requester closes the
// ping with reset. A ping sent to the second boot is also open when the restart is seen, and is reset too; a ping sent after that is answered.
void TestPingOpenAtPeerRestartIsReset() {
	ResetPingState();
	ReportingPacketLine line(VUB::Faults{});
	PingRequester near(line.a);
	std::optional<PingResponder> far;
	far.emplace(line.b);
	WireLog log;
	Establish(line, near, *far, log);

	SIM_EXPECT(SendPing(near, 1000, 0) == ResultStatus::ok);
	testNowUs = 1000;
	near.Process();
	Tee<PingResponder> forward{&log.toFar, &*far, 0};
	line.DeliverToB(forward);

	// The second boot has its own session value, so the requester sees a restart and not the old boot's replies. Its sending session starts
	// as the first boot's does: its first send is busy, and the sync goes out in the next Process.
	far.reset();
	far.emplace(line.b);
	GiveSession(*far, ReliableSession + 1);
	std::array<uint8, 1> trigger = {0xEE};
	SIM_EXPECT(far->template LayerOutbox<0>().Send(std::span<const uint8>(trigger.data(), trigger.size())) == ResultStatus::busy);
	SIM_EXPECT(SendPing(near, 2000, 0) == ResultStatus::ok);
	uint32 now = 2000;
	for (int step = 0; step < 40 && pingCalls < 2; step++) {
		Round(line, near, *far, log, now);
		now += 100000;
	}

	SIM_EXPECT(pingCalls == 2);
	SIM_EXPECT(pingResults[0] == ResultStatus::reset);
	SIM_EXPECT(pingResults[1] == ResultStatus::reset);
	SIM_EXPECT(SendPing(near, now, 0) == ResultStatus::ok);
	for (int step = 0; step < 40 && pingCalls < 3; step++) {
		Round(line, near, *far, log, now);
		now += 100000;
	}

	SIM_EXPECT(pingCalls == 3);
	SIM_EXPECT(pingResults[2] == ResultStatus::ok);
}


// A ping sent while the link is down waits for the link: it is sent when the link returns, and its reply ends it.
void TestPingWhileLinkIsDownWaitsForTheLink() {
	ResetPingState();
	ReportingPacketLine line(VUB::Faults{});
	PingRequester near(line.a);
	PingResponder far(line.b);
	WireLog log;
	Establish(line, near, far, log);

	line.SetLinkUp(false);
	SIM_EXPECT(SendPing(near, 2000, 0) == ResultStatus::ok);
	Round(line, near, far, log, 2000);
	SIM_EXPECT(pingCalls == 0);
	line.SetLinkUp(true);
	uint32 now = 2100;
	for (int step = 0; step < 40 && pingCalls < 1; step++) {
		Round(line, near, far, log, now);
		now += 100000;
	}

	SIM_EXPECT(pingCalls == 1);
	SIM_EXPECT(pingResults[0] == ResultStatus::ok);
}


// A ping whose handler sends a second ping: the second ping is accepted at once and answered by the next Process.
void TestPingFromInsideAFinalHandler() {
	ResetPingState();
	chainSent = false;
	chainStatus = ResultStatus::notAvailable;
	secondCalls = 0;
	secondResult = ResultStatus::notAvailable;
	ReportingPacketLine line(VUB::Faults{});
	PingRequester near(line.a);
	PingResponder far(line.b);
	WireLog log;
	Establish(line, near, far, log);
	chainNear = &near;

	PingHandle handle = near.Requests();
	SIM_EXPECT(handle.template Ping<&OnPingChained>(0) == ResultStatus::ok);
	Round(line, near, far, log, 1000);
	Round(line, near, far, log, 1000);
	Round(line, near, far, log, 1000);
	SIM_EXPECT(pingCalls == 1);
	SIM_EXPECT(chainSent);
	SIM_EXPECT(chainStatus == ResultStatus::ok);
	SIM_EXPECT(secondCalls == 1);
	SIM_EXPECT(secondResult == ResultStatus::ok);
	chainNear = nullptr;
}


// Reliable gives up ahead of a long deadline: the ping ends with retryExhausted, and the deadline calls nothing afterwards.
void TestPingRetryExhaustedBeforeLongDeadline() {
	ResetPingState();
	ReportingPacketLine line(VUB::Faults{});
	PingLongRequester near(line.a);
	PingResponder far(line.b);
	WireLog log;
	Establish(line, near, far, log);

	SIM_EXPECT(SendPing(near, 1000, 0) == ResultStatus::ok);
	uint32 now = 1000;
	for (uint32 step = 0; step < 25; step++) {
		testNowUs = now;
		near.Process();
		now += 100000;
	}

	SIM_EXPECT(pingCalls == 1);
	SIM_EXPECT(pingResults[0] == ResultStatus::retryExhausted);
	testNowUs = 6000000;
	near.Process();
	SIM_EXPECT(pingCalls == 1);
}


// A layer with no Reliable below: a ping is refused noInit until its session is given, and is answered after it.
void TestPingWithoutReliableNeedsASession() {
	ResetPingState();
	ReportingPacketLine line(VUB::Faults{});
	PlainPingRequester near(line.a);
	PlainPingResponder far(line.b);
	WireLog log;
	decltype(near.Requests()) handle = near.Requests();
	SIM_EXPECT(SendPing(near, 1000, 0) == ResultStatus::noInit);
	handle.SetSession(5);
	SIM_EXPECT(SendPing(near, 1000, 0) == ResultStatus::ok);
	Round(line, near, far, log, 1000);
	Round(line, near, far, log, 1000);
	SIM_EXPECT(pingCalls == 1);
	SIM_EXPECT(pingResults[0] == ResultStatus::ok);
}


// A side that only answers pings, with nothing served, answers a ping.
void TestPingAnsweredBySideThatServesNothing() {
	ResetPingState();
	ReportingPacketLine line(VUB::Faults{});
	PingRequester near(line.a);
	PingAnswerOnlyResponder far(line.b);
	WireLog log;
	Establish(line, near, far, log);

	SIM_EXPECT(SendPing(near, 1000, 16) == ResultStatus::ok);
	Round(line, near, far, log, 1000);
	Round(line, near, far, log, 1000);
	SIM_EXPECT(pingCalls == 1);
	SIM_EXPECT(pingResults[0] == ResultStatus::ok);
}


// With Refusals(0), the second of two pings in one burst is dropped: its requester gets no reply, and its deadline ends it.
void TestPingSecondWhileBufferUnsentIsDroppedWithoutRefusals() {
	ResetPingState();
	ReportingPacketLine line(VUB::Faults{});
	PingRequester near(line.a);
	PingDroppingResponder far(line.b);
	WireLog log;
	Establish(line, near, far, log);

	SIM_EXPECT(SendPing(near, 1000, 0) == ResultStatus::ok);
	SIM_EXPECT(SendPing(near, 1000, 0) == ResultStatus::ok);
	Round(line, near, far, log, 1000);
	Round(line, near, far, log, 1000);
	SIM_EXPECT(pingCalls == 1);
	SIM_EXPECT(pingResults[0] == ResultStatus::ok);
	uint32 now = 1000;
	for (int step = 0; step < 20 && pingCalls < 2; step++) {
		now += 100000;
		Round(line, near, far, log, now);
	}

	SIM_EXPECT(pingCalls == 2);
	SIM_EXPECT(pingResults[1] == ResultStatus::timeout);
}


// --- snippet: Layer/Requests.md, ping ---
// The requester: four slots, a ping of up to 16 bytes of padding, and the default deadline of 1000 ms.
using PingLink = VUB::Stack<+[]() {
	return VUB::Builder()
		.Layer<VUB::Requests<PingCommands, VUB::Handlers<>>>().Open(4)
			.Ping(16)
		.Layer<VUB::ReliableOf<40, 4>>().Retries(3).TimeoutMs(200).Window(4)
		.Link<ReportingPacketEnd>()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


// The handler of a ping: the result, and the round trip in microseconds (0 unless the result is ok).
void OnPingDone(ResultStatus result, uint32 roundTripUs) {
	(void)result;
	(void)roundTripUs;
}


// A ping with 8 bytes of padding. The request goes out in the next Process, and the handler runs when the reply has come.
inline ResultStatus SendPingExample(PingLink& link) {
	return link.Requests().Ping<&OnPingDone>(8);
}
// --- end snippet ---


int main() {
	TestPingBytesOnTheWire();
	TestPingPaddingIsEchoedInTheReply();
	TestPingPaddingAboveMaximumIsRefused();
	TestPingStampIsTheClockOfTheProcessThatSendsIt();
	TestPingRoundTripFromEchoedStamp();
	TestPingToPeerWithoutPingIsNotSupported();
	TestPingLostIsRetryExhausted();
	TestPingTimesOutAtDefaultDeadline();
	TestPingBuiltInTimeoutOfTheStackIsUsed();
	TestPingFinalWithoutStatusIsDroppedAndEndsAtDeadline();
	TestPingReplyWithShortStampIsInvalidData();
	TestPingSecondWhileReplyIsUnsentIsBusy();
	TestPingWithoutFreeSlotIsBusy();
	TestPingFromBothSidesAtOnce();
	TestPingUsesNoTicketAndKeepsStartInFlight();
	TestPingLateReplyIsDroppedAfterDeadline();
	TestPingOpenAtPeerRestartIsReset();
	TestPingWhileLinkIsDownWaitsForTheLink();
	TestPingFromInsideAFinalHandler();
	TestPingRetryExhaustedBeforeLongDeadline();
	TestPingWithoutReliableNeedsASession();
	TestPingAnsweredBySideThatServesNothing();
	TestPingSecondWhileBufferUnsentIsDroppedWithoutRefusals();
	return SimFinish("TestVubRequestsPing");
}
