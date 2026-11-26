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

// The events of the requests layer: the bytes of an EVENT, the handlers with and without a body, the order of events, unacknowledged
// events, Delivered events, the events a receiver refuses or cannot read, and the events of a link that is down. Each test starts from zero.
// The clock is testNowUs of Chains.h.


// A newer version of Event::fault: one more field than the receiver's MotorFault knows. The field is appended, as the versioning rule says.
struct NewerMotorFault {
	uint8 code = 0;
	uint16 extra = 0;
};


namespace VUB {

template<>
struct BodyOf<NewerMotorFault> {
	static consteval auto Layout() {
		return Fields<NewerMotorFault>()
			.Field<&NewerMotorFault::code>()
			.Field<&NewerMotorFault::extra>();
	}
};

}


// The command ids and the event ids of this file. The value is the id on the wire.
enum class Command : uint8 {
	stop = 0x13,
	reset = 0x14
};


enum class Event : uint8 {
	fault = 0x40,
	ready = 0x41,
	alarm = 0x42,
	extra = 0x7A
};


// The events of the device. Event::alarm is Delivered: sequenced, and its loss is counted. Ids 0x40 to 0x42.
using EventCommands = VUB::CommandSet<+[]() {
	return VUB::Commands()
		.Event<Event::fault>()
			.Body<Motor::MotorFault>()
		.Event<Event::ready>()
			.Body<VUB::NoBody>()
		.Event<Event::alarm>()
			.Body<Motor::MotorFault>()
			.Delivered();
}>;


// A sender's set with an event the receiver does not know: id 0x7A (Event::extra) has no entry in EventCommands.
using ExtraEventCommands = VUB::CommandSet<+[]() {
	return VUB::Commands()
		.Event<Event::fault>()
			.Body<Motor::MotorFault>()
		.Event<Event::extra>()
			.Body<Motor::MotorFault>();
}>;


// A sender's set whose Event::fault has no body: its packet is the id alone, which a receiver with a MotorFault body reads as short.
using ShortEventCommands = VUB::CommandSet<+[]() {
	return VUB::Commands()
		.Event<Event::fault>()
			.Body<VUB::NoBody>();
}>;


// A newer peer's set: Event::fault with the newer body.
using NewerEventCommands = VUB::CommandSet<+[]() {
	return VUB::Commands()
		.Event<Event::fault>()
			.Body<NewerMotorFault>();
}>;


// The events of a link with no Reliable below: no event is Delivered, since Delivered needs Reliable.
using PlainEventCommands = VUB::CommandSet<+[]() {
	return VUB::Commands()
		.Event<Event::fault>()
			.Body<Motor::MotorFault>()
		.Event<Event::ready>()
			.Body<VUB::NoBody>();
}>;


// A Fire command (no ticket, no reply) and one event, for the order of a burst.
using FireCommands = VUB::CommandSet<+[]() {
	return VUB::Commands()
		.Command<Command::reset>()
			.Fire()
		.Event<Event::fault>()
			.Body<Motor::MotorFault>();
}>;


// A cancel-free request (Command::stop, with its reply status) and the fault event, for a request and an event in one burst.
using BurstCommands = VUB::CommandSet<+[]() {
	return VUB::Commands()
		.Command<Command::stop>()
			.Reply<Motor::MotorStatus>()
			.ReplyTimeoutMs(2000)
			.TicketTimeoutMs(2000)
		.Event<Event::fault>()
			.Body<Motor::MotorFault>();
}>;


using BurstTicket = VUB::Ticket<BurstCommands, Command::stop>;


// The callbacks record each call in a log, in order. A call is one of the Call kinds, with the code of a fault or 0.
constexpr uint8 CallFault = 1;
constexpr uint8 CallReady = 2;
constexpr uint8 CallAlarm = 3;
constexpr uint8 CallReset = 4;
constexpr uint8 CallStop = 5;
constexpr uint8 CallBoth = 6;

inline std::size_t callCount = 0;
inline std::array<uint8, 64> callKinds{};
inline std::array<uint8, 64> callValues{};
inline std::size_t faultCalls = 0;
inline std::size_t readyCalls = 0;
inline std::size_t resetCalls = 0;
inline std::size_t stopCalls = 0;
inline std::size_t stopFinals = 0;
inline std::array<uint16, 256> alarmCalls{};
inline uint8 lastFaultCode = 0;


void LogCall(uint8 kind, uint8 value) {
	if (callCount < callKinds.size()) {
		callKinds[callCount] = kind;
		callValues[callCount] = value;
	}

	callCount++;
}


void OnFault(const Motor::MotorFault& fault) {
	faultCalls++;
	lastFaultCode = fault.code;
	LogCall(CallFault, fault.code);
}


void OnReady() {
	readyCalls++;
	LogCall(CallReady, 0);
}


void OnAlarm(const Motor::MotorFault& alarm) {
	alarmCalls[alarm.code]++;
	LogCall(CallAlarm, alarm.code);
}


void OnReset() {
	resetCalls++;
	LogCall(CallReset, 0);
}


void OnBoth() {
	LogCall(CallBoth, 0);
}


// The device's handler for Command::stop: it takes the ticket and finishes it at once.
void OnStopTicket(BurstTicket ticket) {
	stopCalls++;
	LogCall(CallStop, 0);
	(void)ticket.Finish(Motor::MotorStatus::started);
}


// The requester's final for Command::stop: counted once per request.
void OnStopFinal(const VUB::Final<Motor::MotorStatus, VUB::NoBody>& reply) {
	(void)reply;
	stopFinals++;
}


// The receiver of EventCommands: a handler for each event.
using EventHandlers = VUB::Handlers<
	VUB::OnEvent<Event::fault, &OnFault>,
	VUB::OnEvent<Event::ready, &OnReady>,
	VUB::OnEvent<Event::alarm, &OnAlarm>>;


// A receiver with no handler for Event::ready: an event it does not handle is refused and counted.
using NoReadyHandlers = VUB::Handlers<
	VUB::OnEvent<Event::fault, &OnFault>,
	VUB::OnEvent<Event::alarm, &OnAlarm>>;


using PlainHandlers = VUB::Handlers<
	VUB::OnEvent<Event::fault, &OnFault>,
	VUB::OnEvent<Event::ready, &OnReady>>;


using FireHandlers = VUB::Handlers<
	VUB::On<Command::reset, &OnReset>,
	VUB::OnEvent<Event::fault, &OnFault>>;


// The Reliable layer of the event stacks. The window is 16, so a burst of Delivered events never waits for a slot.
using EventReliable = VUB::ReliableOf<40, 16>;


// Events from the device: no handler on this side, and the Reliable layer below.
using EventSender = VUB::Stack<+[]() {
	return VUB::Builder()
		.CountDrops()
		.SendQueue(4)
		.Layer<VUB::Requests<EventCommands, VUB::Handlers<>>>().Counted()
		.Layer<EventReliable>().Retries(3).TimeoutMs(200).Window(16)
		.Link<ReportingPacketEnd>()
		.MaxPayload(40)
		.ReceiveQueue(64)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


// Events from the device under WhileDown(Fail): an event sent while the link is down is refused and nothing is queued.
using FailingEventSender = VUB::Stack<+[]() {
	return VUB::Builder()
		.CountDrops()
		.SendQueue(4)
		.Layer<VUB::Requests<EventCommands, VUB::Handlers<>>>().Counted()
		.Layer<EventReliable>().Retries(3).TimeoutMs(200).Window(16)
		.Link<ReportingPacketEnd>()
		.MaxPayload(40)
		.ReceiveQueue(64)
		.WhileDown(VUB::Down::Fail)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


// Events from a device whose Event::fault has no body, so the packet of Event::fault is two bytes.
using ShortEventSender = VUB::Stack<+[]() {
	return VUB::Builder()
		.CountDrops()
		.SendQueue(4)
		.Layer<VUB::Requests<ShortEventCommands, VUB::Handlers<>>>().Counted()
		.Layer<EventReliable>().Retries(3).TimeoutMs(200).Window(16)
		.Link<ReportingPacketEnd>()
		.MaxPayload(40)
		.ReceiveQueue(64)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


// Events to the PC: the handlers of EventCommands, and the counters.
using EventReceiver = VUB::Stack<+[]() {
	return VUB::Builder()
		.CountDrops()
		.SendQueue(4)
		.Layer<VUB::Requests<EventCommands, EventHandlers>>().Counted()
		.Layer<EventReliable>().Retries(3).TimeoutMs(200).Window(16)
		.Link<ReportingPacketEnd>()
		.MaxPayload(40)
		.ReceiveQueue(64)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


// A receiver that has no handler for Event::ready.
using NoReadyReceiver = VUB::Stack<+[]() {
	return VUB::Builder()
		.CountDrops()
		.SendQueue(4)
		.Layer<VUB::Requests<EventCommands, NoReadyHandlers>>().Counted()
		.Layer<EventReliable>().Retries(3).TimeoutMs(200).Window(16)
		.Link<ReportingPacketEnd>()
		.MaxPayload(40)
		.ReceiveQueue(64)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


// A newer peer's sender: the events of NewerEventCommands.
using NewerSender = VUB::Stack<+[]() {
	return VUB::Builder()
		.CountDrops()
		.SendQueue(4)
		.Layer<VUB::Requests<NewerEventCommands, VUB::Handlers<>>>().Counted()
		.Layer<EventReliable>().Retries(3).TimeoutMs(200).Window(16)
		.Link<ReportingPacketEnd>()
		.MaxPayload(40)
		.ReceiveQueue(64)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


// A sender that also emits Event::extra, which the receivers do not know.
using ExtraSender = VUB::Stack<+[]() {
	return VUB::Builder()
		.CountDrops()
		.SendQueue(4)
		.Layer<VUB::Requests<ExtraEventCommands, VUB::Handlers<>>>().Counted()
		.Layer<EventReliable>().Retries(3).TimeoutMs(200).Window(16)
		.Link<ReportingPacketEnd>()
		.MaxPayload(40)
		.ReceiveQueue(64)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


// A link with no Reliable below the layer: the events are plain packets.
using PlainEventStack = VUB::Stack<+[]() {
	return VUB::Builder()
		.CountDrops()
		.SendQueue(4)
		.Layer<VUB::Requests<PlainEventCommands, PlainHandlers>>()
		.Link<ReportingPacketEnd>()
		.MaxPayload(40)
		.ReceiveQueue(64)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


// Both ends of a stack with a Fire command and an event.
using FireStack = VUB::Stack<+[]() {
	return VUB::Builder()
		.CountDrops()
		.SendQueue(4)
		.Layer<VUB::Requests<FireCommands, FireHandlers>>().Counted()
		.Layer<EventReliable>().Retries(3).TimeoutMs(200).Window(16)
		.Link<ReportingPacketEnd>()
		.MaxPayload(40)
		.ReceiveQueue(64)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


// The device side of BurstCommands: the handler of Command::stop and the handler of the fault event.
using BurstHandlers = VUB::Handlers<
	VUB::On<Command::stop, &OnStopTicket>,
	VUB::OnEvent<Event::fault, &OnFault>>;


// Both ends of a stack with the request of BurstCommands and its fault event. Each end can send a request and can take tickets.
using BurstStack = VUB::Stack<+[]() {
	return VUB::Builder()
		.CountDrops()
		.SendQueue(4)
		.Layer<VUB::Requests<BurstCommands, BurstHandlers>>().Open(4).Tickets(4)
		.Layer<EventReliable>().Retries(3).TimeoutMs(200).Window(16)
		.Link<ReportingPacketEnd>()
		.MaxPayload(40)
		.ReceiveQueue(64)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


// Gives a stack's Reliable layer its session value: the application does this once per boot.
template<typename StackT>
void GiveEventSession(StackT& stack, uint16 value = ReliableSession) {
	stack.template Find<EventReliable>().SetSession(value);
}


// Sends the packets of one direction to the far stack, and keeps a copy of each. A packet is kept but not delivered while deliver is
// false: it is lost on the line.
template<typename StackT>
struct GatedTee {
	ReliableCapture* capture = nullptr;
	StackT* target = nullptr;
	bool deliver = true;


	void FeedPacket(std::span<const uint8> packet) {
		capture->FeedPacket(packet);

		if (deliver) {
			target->FeedPacket(packet);
		}
	}
};


// The packets of one round, by direction: toFar carries the near end's packets, toNear the far end's.
struct WireLog {
	ReliableCapture toFar;
	ReliableCapture toNear;


	void Clear() {
		toFar = ReliableCapture{};
		toNear = ReliableCapture{};
	}
};


// One round: the near end runs, its packets reach the far end unless deliverToFar is false, the far end runs, its packets reach the near
// end, and the near end runs again.
template<typename NearStack, typename FarStack>
void RunRound(ReportingPacketLine& line, NearStack& near, FarStack& far, WireLog& log, bool deliverToFar, uint32 nowUs) {
	testNowUs = nowUs;
	log.Clear();
	near.Process();
	GatedTee<FarStack> forward{&log.toFar, &far, deliverToFar};
	line.DeliverToB(forward);
	far.Process();
	GatedTee<NearStack> back{&log.toNear, &near, true};
	line.DeliverToA(back);
	near.Process();
}


// Gives both ends a session and runs a few rounds, and then the test sends. The first sequenced packet of a stack carries its sync.
template<typename NearStack, typename FarStack>
void Establish(ReportingPacketLine& line, NearStack& near, FarStack& far, WireLog& log) {
	GiveEventSession(near);
	GiveEventSession(far);
	RunRound(line, near, far, log, true, 50);

	for (uint32 round = 0; round < 3; round++) {
		RunRound(line, near, far, log, true, 100 + round);
	}
}


// The first sequenced send of a session is busy: Reliable starts the session with that send, and queues nothing. The rounds that follow carry
// the sync and its acknowledgement, so the sequenced sends after them are accepted. A Delivered event or a request is sent only after this.
template<typename NearStack, typename FarStack>
void StartSequencedSession(ReportingPacketLine& line, NearStack& near, FarStack& far, WireLog& log, uint32 nowUs) {
	for (uint32 round = 0; round < 4; round++) {
		RunRound(line, near, far, log, true, nowUs + round);
	}
}


// True when the payload of packet index of the capture ends with the expected bytes (the layer's payload follows Reliable's header).
template<std::size_t Size>
bool EndsWithBytes(const ReliableCapture& capture, std::size_t index, const std::array<uint8, Size>& tail) {
	if (index >= capture.count || index >= capture.packets.size() || capture.lengths[index] < Size) {
		return false;
	}

	std::size_t start = capture.lengths[index] - Size;

	for (std::size_t byte = 0; byte < Size; byte++) {
		if (capture.packets[index][start + byte] != tail[byte]) {
			return false;
		}
	}

	return true;
}


// The number of packets of the capture that end with the expected bytes.
template<std::size_t Size>
std::size_t CountEndingWith(const ReliableCapture& capture, const std::array<uint8, Size>& tail) {
	std::size_t found = 0;
	std::size_t limit = capture.count < capture.packets.size() ? capture.count : capture.packets.size();

	for (std::size_t index = 0; index < limit; index++) {
		if (EndsWithBytes(capture, index, tail)) {
			found++;
		}
	}

	return found;
}


Motor::MotorFault FaultWithCode(uint8 code) {
	Motor::MotorFault fault{};
	fault.code = code;
	return fault;
}


void ResetEventState() {
	ResetReliableState();
	callCount = 0;
	callKinds.fill(0);
	callValues.fill(0);
	faultCalls = 0;
	readyCalls = 0;
	resetCalls = 0;
	stopCalls = 0;
	stopFinals = 0;
	alarmCalls.fill(0);
	lastFaultCode = 0;
}


// An event on a link with no Reliable below is one plain packet: its id, then its body. Event::fault with code 3 is 04 40 03, and an event
// with no body is 04 41.
void TestEventsOnPlainLinkAreExactBytes() {
	ResetEventState();
	ReportingPacketLine line(VUB::Faults{});
	PlainEventStack near(line.a);
	PlainEventStack far(line.b);
	WireLog log;
	RunRound(line, near, far, log, true, 1000);

	auto handle = near.Requests();
	SIM_EXPECT(handle.Emit<Event::fault>(FaultWithCode(3)) == ResultStatus::ok);
	RunRound(line, near, far, log, true, 2000);
	std::array<uint8, 3> fault = {0x04, 0x40, 0x03};
	SIM_EXPECT(log.toFar.count == 1);
	SIM_EXPECT(CaptureIs(log.toFar, 0, fault));
	SIM_EXPECT(faultCalls == 1);
	SIM_EXPECT(lastFaultCode == 3);

	SIM_EXPECT(handle.Emit<Event::ready>(VUB::NoBody{}) == ResultStatus::ok);
	RunRound(line, near, far, log, true, 3000);
	std::array<uint8, 2> ready = {0x04, 0x41};
	SIM_EXPECT(log.toFar.count == 1);
	SIM_EXPECT(CaptureIs(log.toFar, 0, ready));
	SIM_EXPECT(readyCalls == 1);
}


// The form without an argument of an event with no body: Emit<Name>() sends the same bytes as Emit<Name>(NoBody{}).
void TestNoBodyEventEmitsWithoutArgument() {
	ResetEventState();
	ReportingPacketLine line(VUB::Faults{});
	EventSender near(line.a);
	EventReceiver far(line.b);
	WireLog log;
	Establish(line, near, far, log);

	auto handle = near.Requests();
	SIM_EXPECT(handle.Emit<Event::ready>() == ResultStatus::ok);
	RunRound(line, near, far, log, true, 1000);
	std::array<uint8, 2> ready = {0x04, 0x41};
	SIM_EXPECT(log.toFar.count == 1);
	SIM_EXPECT(EndsWithBytes(log.toFar, 0, ready));
	SIM_EXPECT(readyCalls == 1);
}


// An unacknowledged event on a Reliable link is one packet with its id and body at the end of its payload. The receiver owes no
// acknowledgement for it, so no packet comes back in the round.
void TestUnacknowledgedEventIsOnePacketWithNoAcknowledgement() {
	ResetEventState();
	ReportingPacketLine line(VUB::Faults{});
	EventSender near(line.a);
	EventReceiver far(line.b);
	WireLog log;
	Establish(line, near, far, log);

	auto handle = near.Requests();
	SIM_EXPECT(handle.Emit<Event::fault>(FaultWithCode(9)) == ResultStatus::ok);
	RunRound(line, near, far, log, true, 1000);
	std::array<uint8, 3> fault = {0x04, 0x40, 0x09};
	SIM_EXPECT(log.toFar.count == 1);
	SIM_EXPECT(EndsWithBytes(log.toFar, 0, fault));
	SIM_EXPECT(log.toNear.count == 0);
	SIM_EXPECT(faultCalls == 1);
	SIM_EXPECT(lastFaultCode == 9);
}


// An unacknowledged event whose packet is lost is not sent again: the timers of Reliable run on, and no copy follows.
void TestUnacknowledgedEventIsNotResentAfterLoss() {
	ResetEventState();
	ReportingPacketLine line(VUB::Faults{});
	EventSender near(line.a);
	EventReceiver far(line.b);
	WireLog log;
	Establish(line, near, far, log);

	auto handle = near.Requests();
	SIM_EXPECT(handle.Emit<Event::fault>(FaultWithCode(4)) == ResultStatus::ok);
	std::array<uint8, 3> fault = {0x04, 0x40, 0x04};
	std::size_t copies = 0;
	uint32 now = 1000;

	for (int step = 0; step < 12; step++) {
		// The first round loses the packet on the line. The rounds after it deliver what is left, while the timers run on.
		RunRound(line, near, far, log, step != 0, now);
		copies += CountEndingWith(log.toFar, fault);
		now += 100000;
	}

	SIM_EXPECT(copies == 1);
	SIM_EXPECT(faultCalls == 0);
}


// The handler with a body and the handler without one each run once, with the body's value.
void TestEventHandlersWithAndWithoutBodyBothRun() {
	ResetEventState();
	ReportingPacketLine line(VUB::Faults{});
	EventSender near(line.a);
	EventReceiver far(line.b);
	WireLog log;
	Establish(line, near, far, log);

	auto handle = near.Requests();
	SIM_EXPECT(handle.Emit<Event::fault>(FaultWithCode(7)) == ResultStatus::ok);
	SIM_EXPECT(handle.Emit<Event::ready>(VUB::NoBody{}) == ResultStatus::ok);
	RunRound(line, near, far, log, true, 1000);
	SIM_EXPECT(faultCalls == 1);
	SIM_EXPECT(lastFaultCode == 7);
	SIM_EXPECT(readyCalls == 1);
}


// Events that arrive in one burst run in the order they were sent.
void TestEventsOfOneBurstRunInArrivalOrder() {
	ResetEventState();
	ReportingPacketLine line(VUB::Faults{});
	EventSender near(line.a);
	EventReceiver far(line.b);
	WireLog log;
	Establish(line, near, far, log);

	auto handle = near.Requests();
	SIM_EXPECT(handle.Emit<Event::fault>(FaultWithCode(1)) == ResultStatus::ok);
	SIM_EXPECT(handle.Emit<Event::ready>(VUB::NoBody{}) == ResultStatus::ok);
	SIM_EXPECT(handle.Emit<Event::fault>(FaultWithCode(2)) == ResultStatus::ok);
	RunRound(line, near, far, log, true, 1000);
	SIM_EXPECT(callCount == 3);
	SIM_EXPECT(callKinds[0] == CallFault);
	SIM_EXPECT(callValues[0] == 1);
	SIM_EXPECT(callKinds[1] == CallReady);
	SIM_EXPECT(callKinds[2] == CallFault);
	SIM_EXPECT(callValues[2] == 2);
}


// A Fire and an event of one burst run in arrival order: the Fire is sent first, so its handler runs first, and the event after it.
// A Fire takes no ticket and runs at once in Take, after the requests received earlier in the same walk.
void TestFireAndEventOfOneBurstRunInArrivalOrder() {
	ResetEventState();
	ReportingPacketLine line(VUB::Faults{});
	FireStack near(line.a);
	FireStack far(line.b);
	WireLog log;
	Establish(line, near, far, log);

	auto handle = near.Requests();
	SIM_EXPECT(handle.Fire<Command::reset>(VUB::NoBody{}) == ResultStatus::ok);
	SIM_EXPECT(handle.Emit<Event::fault>(FaultWithCode(5)) == ResultStatus::ok);
	RunRound(line, near, far, log, true, 1000);
	SIM_EXPECT(callCount == 2);
	SIM_EXPECT(callKinds[0] == CallReset);
	SIM_EXPECT(callKinds[1] == CallFault);
	SIM_EXPECT(resetCalls == 1);
	SIM_EXPECT(faultCalls == 1);
	SIM_EXPECT(lastFaultCode == 5);
}


// A newer peer sends one more field than the receiver knows: the receiver reads code and ignores the two bytes after it.
void TestNewerEventBodyIsReadAsFarAsTheReceiverKnows() {
	ResetEventState();
	ReportingPacketLine line(VUB::Faults{});
	NewerSender near(line.a);
	EventReceiver far(line.b);
	WireLog log;
	Establish(line, near, far, log);

	auto handle = near.Requests();
	NewerMotorFault newer{};
	newer.code = 3;
	newer.extra = 0x1234;
	SIM_EXPECT(handle.Emit<Event::fault>(newer) == ResultStatus::ok);
	RunRound(line, near, far, log, true, 1000);
	std::array<uint8, 5> wire = {0x04, 0x40, 0x03, 0x34, 0x12};
	SIM_EXPECT(log.toFar.count == 1);
	SIM_EXPECT(EndsWithBytes(log.toFar, 0, wire));
	SIM_EXPECT(faultCalls == 1);
	SIM_EXPECT(lastFaultCode == 3);
}


// An event whose body is shorter than the receiver's body is not delivered. The receiver reads a MotorFault from a body of none, so the
// packet is dropped and counted in layerError. It is not a refusal: the event id is known.
void TestShortEventBodyIsNotDeliveredAndCountedAsLayerError() {
	ResetEventState();
	ReportingPacketLine line(VUB::Faults{});
	ShortEventSender near(line.a);
	EventReceiver far(line.b);
	WireLog log;
	Establish(line, near, far, log);

	auto handle = near.Requests();
	SIM_EXPECT(handle.Emit<Event::fault>() == ResultStatus::ok);
	RunRound(line, near, far, log, true, 1000);
	std::array<uint8, 2> shortFault = {0x04, 0x40};
	SIM_EXPECT(log.toFar.count == 1);
	SIM_EXPECT(EndsWithBytes(log.toFar, 0, shortFault));
	SIM_EXPECT(faultCalls == 0);
	SIM_EXPECT(far.Drops().layerError == 1);

	auto farHandle = far.Requests();
	SIM_EXPECT(farHandle.Refused() == 0);
}


// A Delivered event that is acknowledged is handed to its handler once, and its loss is not counted.
void TestDeliveredEventAcknowledgedIsDeliveredOnceAndNotCounted() {
	ResetEventState();
	ReportingPacketLine line(VUB::Faults{});
	EventSender near(line.a);
	EventReceiver far(line.b);
	WireLog log;
	Establish(line, near, far, log);

	auto handle = near.Requests();
	SIM_EXPECT(handle.Emit<Event::alarm>(FaultWithCode(0)) == ResultStatus::busy);
	StartSequencedSession(line, near, far, log, 900);
	SIM_EXPECT(handle.Emit<Event::alarm>(FaultWithCode(1)) == ResultStatus::ok);
	uint32 now = 1000;

	for (int step = 0; step < 4; step++) {
		RunRound(line, near, far, log, true, now);
		now += 1000;
	}

	SIM_EXPECT(alarmCalls[1] == 1);
	SIM_EXPECT(handle.EventsLost() == 0);
}


// A line that loses and duplicates a fifth of its packets: each Delivered event reaches its handler at most once, and every event either
// reaches it or is counted in eventsLost.
void TestDeliveredEventsEachDeliveredOnceUnderLossAndDuplication() {
	ResetEventState();
	VUB::Faults faults{};
	faults.seed = 7;
	faults.dropPercent = 20;
	faults.duplicatePercent = 20;
	ReportingPacketLine line(faults);
	EventSender near(line.a);
	EventReceiver far(line.b);
	WireLog log;
	Establish(line, near, far, log);

	auto handle = near.Requests();
	SIM_EXPECT(handle.Emit<Event::alarm>(FaultWithCode(0)) == ResultStatus::busy);
	StartSequencedSession(line, near, far, log, 900);
	uint32 now = 1000;

	for (uint8 code = 0; code < 10; code++) {
		SIM_EXPECT(handle.Emit<Event::alarm>(FaultWithCode(code)) == ResultStatus::ok);
		RunRound(line, near, far, log, true, now);
		now += 50000;
	}

	for (int step = 0; step < 200; step++) {
		RunRound(line, near, far, log, true, now);
		now += 50000;
	}

	std::size_t delivered = 0;

	for (uint8 code = 0; code < 10; code++) {
		SIM_EXPECT(alarmCalls[code] <= 1);
		delivered += alarmCalls[code];
	}

	std::size_t lost = handle.EventsLost();
	SIM_EXPECT(delivered + lost >= 10);
}


// A Delivered event whose every packet is lost ends with retryExhausted, and that outcome is counted in eventsLost. The handler never runs.
void TestDeliveredEventRetryExhaustedIsCountedInEventsLost() {
	ResetEventState();
	ReportingPacketLine line(VUB::Faults{});
	EventSender near(line.a);
	EventReceiver far(line.b);
	WireLog log;
	Establish(line, near, far, log);

	auto handle = near.Requests();
	SIM_EXPECT(handle.Emit<Event::alarm>(FaultWithCode(0)) == ResultStatus::busy);
	StartSequencedSession(line, near, far, log, 900);
	SIM_EXPECT(handle.Emit<Event::alarm>(FaultWithCode(2)) == ResultStatus::ok);
	uint32 now = 1000;

	for (int step = 0; step < 12; step++) {
		RunRound(line, near, far, log, false, now);
		now += 100000;
	}

	SIM_EXPECT(handle.EventsLost() == 1);
	SIM_EXPECT(alarmCalls[2] == 0);
}


// A Delivered event still pending at the near end when the far end reboots: the far end's sync of its second session reports the restart, and the pending
// event's outcome is reset, which is counted in eventsLost. The receiver is the same on both ends, so both have handlers and counters.
void TestDeliveredEventPendingAtPeerRebootIsCountedAsReset() {
	ResetEventState();
	ReportingPacketLine line(VUB::Faults{});
	EventReceiver near(line.a);
	std::optional<EventReceiver> far;
	far.emplace(line.b);
	WireLog log;
	Establish(line, near, *far, log);

	// The far end sends one Delivered event on the first session, so the near end knows its session value.
	auto farFirst = far->Requests();
	SIM_EXPECT(farFirst.Emit<Event::alarm>(FaultWithCode(0)) == ResultStatus::busy);
	StartSequencedSession(line, near, *far, log, 900);
	SIM_EXPECT(farFirst.Emit<Event::alarm>(FaultWithCode(7)) == ResultStatus::ok);
	uint32 now = 1000;

	for (int step = 0; step < 2; step++) {
		RunRound(line, near, *far, log, true, now);
		now += 1000;
	}

	SIM_EXPECT(alarmCalls[7] == 1);
	auto handle = near.Requests();
	SIM_EXPECT(handle.Emit<Event::alarm>(FaultWithCode(0)) == ResultStatus::busy);
	StartSequencedSession(line, near, *far, log, now);
	now += 1000;
	SIM_EXPECT(handle.Emit<Event::alarm>(FaultWithCode(6)) == ResultStatus::ok);
	// The alarm's packet is lost on the line, so it stays pending at the near end.
	RunRound(line, near, *far, log, false, now);
	now += 1000;
	SIM_EXPECT(handle.EventsLost() == 0);

	// The far end reboots with session value 0x4321 and sends its own alarm. That sequenced packet reports the restart to the near end.
	far.reset();
	far.emplace(line.b);
	GiveEventSession(*far, 0x4321);
	auto farAgain = far->Requests();
	SIM_EXPECT(farAgain.Emit<Event::alarm>(FaultWithCode(0)) == ResultStatus::busy);
	StartSequencedSession(line, near, *far, log, now);
	now += 1000;
	SIM_EXPECT(farAgain.Emit<Event::alarm>(FaultWithCode(8)) == ResultStatus::ok);

	for (int step = 0; step < 4; step++) {
		RunRound(line, near, *far, log, true, now);
		now += 10000;
	}

	SIM_EXPECT(handle.EventsLost() == 1);
}


// An event id the receiver's table does not hold is not handed to any handler, and it is counted in refused. The event after it still arrives.
void TestUnknownEventIdIsNotDeliveredAndIsRefused() {
	ResetEventState();
	ReportingPacketLine line(VUB::Faults{});
	ExtraSender near(line.a);
	EventReceiver far(line.b);
	WireLog log;
	Establish(line, near, far, log);

	auto handle = near.Requests();
	SIM_EXPECT(handle.Emit<Event::extra>(FaultWithCode(9)) == ResultStatus::ok);
	SIM_EXPECT(handle.Emit<Event::fault>(FaultWithCode(4)) == ResultStatus::ok);
	RunRound(line, near, far, log, true, 1000);
	SIM_EXPECT(callCount == 1);
	SIM_EXPECT(faultCalls == 1);
	SIM_EXPECT(lastFaultCode == 4);

	auto farHandle = far.Requests();
	SIM_EXPECT(farHandle.Refused() == 1);
}


// An event the receiver has no handler for is dropped and counted in refused. The handler of the other event still runs.
void TestEventWithoutHandlerIsRefusedAndNotDelivered() {
	ResetEventState();
	ReportingPacketLine line(VUB::Faults{});
	EventSender near(line.a);
	NoReadyReceiver far(line.b);
	WireLog log;
	Establish(line, near, far, log);

	auto handle = near.Requests();
	SIM_EXPECT(handle.Emit<Event::ready>(VUB::NoBody{}) == ResultStatus::ok);
	SIM_EXPECT(handle.Emit<Event::fault>(FaultWithCode(3)) == ResultStatus::ok);
	RunRound(line, near, far, log, true, 1000);
	SIM_EXPECT(readyCalls == 0);
	SIM_EXPECT(faultCalls == 1);

	auto farHandle = far.Requests();
	SIM_EXPECT(farHandle.Refused() == 1);
}


// Under WhileDown(Fail) an event sent while the link is down is refused with noConnection and nothing is queued: after the link returns,
// the refused event does not go out, and an event sent afterwards does.
void TestEmitUnderFailWhileLinkDownReturnsNoConnectionAndQueuesNothing() {
	ResetEventState();
	ReportingPacketLine line(VUB::Faults{});
	FailingEventSender near(line.a);
	EventReceiver far(line.b);
	WireLog log;
	Establish(line, near, far, log);

	auto handle = near.Requests();
	line.SetLinkUp(false);
	testNowUs = 1000;
	near.Process();
	SIM_EXPECT(handle.Emit<Event::fault>(FaultWithCode(6)) == ResultStatus::noConnection);

	line.SetLinkUp(true);
	RunRound(line, near, far, log, true, 2000);
	RunRound(line, near, far, log, true, 3000);
	SIM_EXPECT(faultCalls == 0);

	SIM_EXPECT(handle.Emit<Event::fault>(FaultWithCode(7)) == ResultStatus::ok);
	RunRound(line, near, far, log, true, 4000);
	SIM_EXPECT(faultCalls == 1);
	SIM_EXPECT(lastFaultCode == 7);
}


// Under WhileDown(Wait), an event sent while the link is down is accepted with ok and waits in the control queue. It goes out when the link
// returns, and the receiver's handler runs once.
void TestEmitUnderWaitWhileLinkDownGoesOutWhenLinkReturns() {
	ResetEventState();
	ReportingPacketLine line(VUB::Faults{});
	EventSender near(line.a);
	EventReceiver far(line.b);
	WireLog log;
	Establish(line, near, far, log);

	auto handle = near.Requests();
	line.SetLinkUp(false);
	RunRound(line, near, far, log, true, 1000);
	SIM_EXPECT(handle.Emit<Event::fault>(FaultWithCode(8)) == ResultStatus::ok);
	RunRound(line, near, far, log, true, 2000);
	RunRound(line, near, far, log, true, 3000);
	SIM_EXPECT(faultCalls == 0);

	line.SetLinkUp(true);
	RunRound(line, near, far, log, true, 4000);
	RunRound(line, near, far, log, true, 5000);
	SIM_EXPECT(faultCalls == 1);
	SIM_EXPECT(lastFaultCode == 8);
}


// When the control queue is full, Emit returns busy and queues nothing. The events that returned ok are the ones the receiver gets.
void TestEmitWhenQueueIsFullReturnsBusyAndLosesNothing() {
	ResetEventState();
	ReportingPacketLine line(VUB::Faults{});
	EventSender near(line.a);
	EventReceiver far(line.b);
	WireLog log;
	Establish(line, near, far, log);

	auto handle = near.Requests();
	std::size_t accepted = 0;
	bool sawBusy = false;

	for (uint32 attempt = 0; attempt < 1000 && !sawBusy; attempt++) {
		ResultStatus result = handle.Emit<Event::fault>(FaultWithCode(static_cast<uint8>(attempt)));

		if (result == ResultStatus::ok) {
			accepted++;
		} else {
			SIM_EXPECT(result == ResultStatus::busy);
			sawBusy = true;
		}
	}

	SIM_EXPECT(sawBusy);

	// The queued events go out in the rounds that follow. The loop is bounded, and each round is one Process of both ends.
	for (uint32 round = 0; round < 8; round++) {
		RunRound(line, near, far, log, true, 1000 + round * 100);
	}

	SIM_EXPECT(faultCalls == accepted);
}


// A Delivered event whose first packet is lost is sent again when Reliable's timer runs out, and its handler runs once. Its loss is not
// counted, since its outcome is ok.
void TestDeliveredEventLostOnceIsResentAndDeliveredOnce() {
	ResetEventState();
	ReportingPacketLine line(VUB::Faults{});
	EventSender near(line.a);
	EventReceiver far(line.b);
	WireLog log;
	Establish(line, near, far, log);

	auto handle = near.Requests();
	std::array<uint8, 3> alarm = {0x04, 0x42, 0x05};
	SIM_EXPECT(handle.Emit<Event::alarm>(FaultWithCode(0)) == ResultStatus::busy);
	StartSequencedSession(line, near, far, log, 900);
	SIM_EXPECT(handle.Emit<Event::alarm>(FaultWithCode(5)) == ResultStatus::ok);
	RunRound(line, near, far, log, false, 1000);
	SIM_EXPECT(alarmCalls[5] == 0);

	// 250 ms later the timer (200 ms) has run out: the packet goes out again and gets through.
	RunRound(line, near, far, log, true, 251000);
	SIM_EXPECT(CountEndingWith(log.toFar, alarm) == 1);
	RunRound(line, near, far, log, true, 252000);
	RunRound(line, near, far, log, true, 253000);
	SIM_EXPECT(alarmCalls[5] == 1);
	SIM_EXPECT(handle.EventsLost() == 0);
}


// A request and an event in one burst. The request is sent first and the event second. The events of a burst run in arrival order, so the
// request's handler runs first, and the event's handler after it. The handler of the request runs once, and its final once on the requester.
void TestRequestAndEventOfOneBurstRunInArrivalOrder() {
	ResetEventState();
	ReportingPacketLine line(VUB::Faults{});
	BurstStack near(line.a);
	BurstStack far(line.b);
	WireLog log;
	Establish(line, near, far, log);

	auto handle = near.Requests();
	auto refused = handle.Send<Command::stop, &OnStopFinal>(VUB::NoBody{});
	SIM_EXPECT(refused.Error() == ResultStatus::busy);
	StartSequencedSession(line, near, far, log, 900);
	auto sent = handle.Send<Command::stop, &OnStopFinal>(VUB::NoBody{});
	SIM_EXPECT(sent.IsOk());
	SIM_EXPECT(handle.Emit<Event::fault>(FaultWithCode(5)) == ResultStatus::ok);
	RunRound(line, near, far, log, true, 1000);
	RunRound(line, near, far, log, true, 2000);
	SIM_EXPECT(callCount == 2);
	SIM_EXPECT(callKinds[0] == CallStop);
	SIM_EXPECT(callKinds[1] == CallFault);
	SIM_EXPECT(stopCalls == 1);
	SIM_EXPECT(stopFinals == 1);
	SIM_EXPECT(faultCalls == 1);
	SIM_EXPECT(lastFaultCode == 5);
}


// A command and an event that have one value: the command's id and the event's id are both 0x40, and the kind byte tells them apart.
enum class SharedId : uint8 {
	both = 0x40
};


using SharedCommands = VUB::CommandSet<+[]() {
	return VUB::Commands()
		.Command<SharedId::both>()
			.Fire()
		.Event<Event::fault>()
			.Body<Motor::MotorFault>();
}>;


using SharedHandlers = VUB::Handlers<
	VUB::On<SharedId::both, &OnBoth>,
	VUB::OnEvent<Event::fault, &OnFault>>;


using SharedStack = VUB::Stack<+[]() {
	return VUB::Builder()
		.CountDrops()
		.SendQueue(4)
		.Layer<VUB::Requests<SharedCommands, SharedHandlers>>()
		.Layer<EventReliable>().Retries(3).TimeoutMs(200).Window(16)
		.Link<ReportingPacketEnd>()
		.MaxPayload(40)
		.ReceiveQueue(64)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


// The event set of the device's side: it emits Event::fault and Event::ready, and handles Event::ready.
using NearSetEvents = VUB::CommandSet<+[]() {
	return VUB::Commands()
		.Event<Event::fault>()
			.Body<Motor::MotorFault>()
		.Event<Event::ready>()
			.Body<VUB::NoBody>();
}>;


using NearSetHandlers = VUB::Handlers<
	VUB::OnEvent<Event::ready, &OnReady>>;


// The event set of the PC's side: it emits Event::alarm (Delivered), and handles Event::fault.
using FarSetEvents = VUB::CommandSet<+[]() {
	return VUB::Commands()
		.Event<Event::fault>()
			.Body<Motor::MotorFault>()
		.Event<Event::alarm>()
			.Body<Motor::MotorFault>()
			.Delivered();
}>;


using FarSetHandlers = VUB::Handlers<
	VUB::OnEvent<Event::fault, &OnFault>>;


using NearSetStack = VUB::Stack<+[]() {
	return VUB::Builder()
		.CountDrops()
		.SendQueue(4)
		.Layer<VUB::Requests<NearSetEvents, NearSetHandlers>>().Counted()
		.Layer<EventReliable>().Retries(3).TimeoutMs(200).Window(16)
		.Link<ReportingPacketEnd>()
		.MaxPayload(40)
		.ReceiveQueue(64)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


using FarSetStack = VUB::Stack<+[]() {
	return VUB::Builder()
		.CountDrops()
		.SendQueue(4)
		.Layer<VUB::Requests<FarSetEvents, FarSetHandlers>>().Counted()
		.Layer<EventReliable>().Retries(3).TimeoutMs(200).Window(16)
		.Link<ReportingPacketEnd>()
		.MaxPayload(40)
		.ReceiveQueue(64)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


// A device that holds each ticket of Command::stop and never finishes it: the ticket's timeout ends it.
void OnStopHold(BurstTicket ticket) {
	(void)ticket;
	stopCalls++;
	LogCall(CallStop, 0);
}


using HoldHandlers = VUB::Handlers<
	VUB::On<Command::stop, &OnStopHold>>;


using HoldStack = VUB::Stack<+[]() {
	return VUB::Builder()
		.CountDrops()
		.SendQueue(4)
		.Layer<VUB::Requests<BurstCommands, HoldHandlers>>().Open(4).Tickets(4).Counted()
		.Layer<EventReliable>().Retries(3).TimeoutMs(200).Window(16)
		.Link<ReportingPacketEnd>()
		.MaxPayload(40)
		.ReceiveQueue(64)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


// A device with one ticket and no refusal ring: a second request while the first is held is refused for lack of room.
using RoomStack = VUB::Stack<+[]() {
	return VUB::Builder()
		.CountDrops()
		.SendQueue(4)
		.Layer<VUB::Requests<BurstCommands, BurstHandlers>>().Open(4).Tickets(1).Refusals(0).Counted()
		.Layer<EventReliable>().Retries(3).TimeoutMs(200).Window(16)
		.Link<ReportingPacketEnd>()
		.MaxPayload(40)
		.ReceiveQueue(64)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


// Command and event with one value: both run, in the order their packets came, and each packet has its own kind.
void TestCommandAndEventWithOneValueBothRun() {
	ResetEventState();
	ReportingPacketLine line(VUB::Faults{});
	SharedStack near(line.a);
	SharedStack far(line.b);
	WireLog log;
	Establish(line, near, far, log);

	auto handle = near.Requests();
	SIM_EXPECT(handle.Fire<SharedId::both>(VUB::NoBody{}) == ResultStatus::ok);
	SIM_EXPECT(handle.Emit<Event::fault>(FaultWithCode(3)) == ResultStatus::ok);
	RunRound(line, near, far, log, true, 1000);
	SIM_EXPECT(callCount == 2);
	SIM_EXPECT(callKinds[0] == CallBoth);
	SIM_EXPECT(callKinds[1] == CallFault);
	SIM_EXPECT(lastFaultCode == 3);
	std::array<uint8, 3> fault = {0x04, 0x40, 0x03};
	SIM_EXPECT(CountEndingWith(log.toFar, fault) == 1);
}


// An event between two requests of one burst: the first request's handler runs, then the event's, then the second request's.
void TestEventBetweenTwoRequestsOfOneBurstRunsInOrder() {
	ResetEventState();
	ReportingPacketLine line(VUB::Faults{});
	BurstStack near(line.a);
	BurstStack far(line.b);
	WireLog log;
	Establish(line, near, far, log);

	auto handle = near.Requests();
	auto refused = handle.Send<Command::stop, &OnStopFinal>(VUB::NoBody{});
	SIM_EXPECT(refused.Error() == ResultStatus::busy);
	StartSequencedSession(line, near, far, log, 900);
	auto first = handle.Send<Command::stop, &OnStopFinal>(VUB::NoBody{});
	SIM_EXPECT(first.IsOk());
	SIM_EXPECT(handle.Emit<Event::fault>(FaultWithCode(5)) == ResultStatus::ok);
	auto second = handle.Send<Command::stop, &OnStopFinal>(VUB::NoBody{});
	SIM_EXPECT(second.IsOk());
	RunRound(line, near, far, log, true, 1000);
	RunRound(line, near, far, log, true, 2000);
	SIM_EXPECT(callCount == 3);
	SIM_EXPECT(callKinds[0] == CallStop);
	SIM_EXPECT(callKinds[1] == CallFault);
	SIM_EXPECT(callKinds[2] == CallStop);
	SIM_EXPECT(stopCalls == 2);
	SIM_EXPECT(stopFinals == 2);
}


// A line that duplicates every packet: a Delivered event reaches its handler once, and its duplicate is not a loss.
void TestDuplicateDeliveredEventIsHandledOnce() {
	ResetEventState();
	VUB::Faults faults{};
	faults.duplicatePercent = 100;
	ReportingPacketLine line(faults);
	EventSender near(line.a);
	EventReceiver far(line.b);
	WireLog log;
	Establish(line, near, far, log);

	auto handle = near.Requests();
	SIM_EXPECT(handle.Emit<Event::alarm>(FaultWithCode(0)) == ResultStatus::busy);
	StartSequencedSession(line, near, far, log, 900);
	SIM_EXPECT(handle.Emit<Event::alarm>(FaultWithCode(6)) == ResultStatus::ok);
	uint32 now = 1000;

	for (int step = 0; step < 4; step++) {
		RunRound(line, near, far, log, true, now);
		now += 1000;
	}

	SIM_EXPECT(alarmCalls[6] == 1);
	SIM_EXPECT(handle.EventsLost() == 0);
}


// More Delivered events than the sequence has numbers: the sequence wraps, and every event arrives once with no loss.
void TestDeliveredEventSequenceWrapsAndEveryEventArrives() {
	ResetEventState();
	ReportingPacketLine line(VUB::Faults{});
	EventSender near(line.a);
	EventReceiver far(line.b);
	WireLog log;
	Establish(line, near, far, log);

	auto handle = near.Requests();
	SIM_EXPECT(handle.Emit<Event::alarm>(FaultWithCode(0)) == ResultStatus::busy);
	StartSequencedSession(line, near, far, log, 900);
	const std::size_t total = 65536 + 16;
	std::size_t sent = 0;
	uint32 now = 1000;

	for (uint32 rounds = 0; sent < total && rounds < 100000; rounds++) {
		uint32 burst = 0;

		while (burst < 8 && sent < total) {
			if (handle.Emit<Event::alarm>(FaultWithCode(static_cast<uint8>(sent))) != ResultStatus::ok) {
				break;
			}

			sent++;
			burst++;
		}

		RunRound(line, near, far, log, true, now);
		now += 1000;
	}

	for (int step = 0; step < 4; step++) {
		RunRound(line, near, far, log, true, now);
		now += 1000;
	}

	std::size_t arrived = 0;
	for (uint16 count : alarmCalls) {
		arrived += count;
	}

	SIM_EXPECT(sent == total);
	SIM_EXPECT(arrived == total);
	SIM_EXPECT(handle.EventsLost() == 0);
}


// The refused counter stops at 65535: more refused events than that leave it there.
void TestRefusedCounterStopsAtTheMaximum() {
	ResetEventState();
	ReportingPacketLine line(VUB::Faults{});
	ExtraSender near(line.a);
	EventReceiver far(line.b);
	WireLog log;
	Establish(line, near, far, log);

	auto handle = near.Requests();
	const std::size_t total = 65540;
	std::size_t sent = 0;
	uint32 now = 1000;

	for (uint32 rounds = 0; sent < total && rounds < 100000; rounds++) {
		uint32 burst = 0;

		while (burst < 8 && sent < total) {
			if (handle.Emit<Event::extra>(FaultWithCode(9)) != ResultStatus::ok) {
				break;
			}

			sent++;
			burst++;
		}

		RunRound(line, near, far, log, true, now);
		now += 1000;
	}

	RunRound(line, near, far, log, true, now);
	auto farHandle = far.Requests();
	SIM_EXPECT(sent == total);
	SIM_EXPECT(farHandle.Refused() == 65535);
	SIM_EXPECT(faultCalls == 0);
}


// Two event sets, one for each direction: the device emits Event::fault and Event::ready, the PC emits Event::alarm (Delivered) and handles
// Event::fault. Each side refuses the event its peer emits that its own set does not have.
void TestTwoEventSetsOneForEachDirection() {
	ResetEventState();
	ReportingPacketLine line(VUB::Faults{});
	NearSetStack near(line.a);
	FarSetStack far(line.b);
	WireLog log;
	Establish(line, near, far, log);

	auto nearHandle = near.Requests();
	auto farHandle = far.Requests();
	SIM_EXPECT(nearHandle.Emit<Event::fault>(FaultWithCode(4)) == ResultStatus::ok);
	SIM_EXPECT(nearHandle.Emit<Event::ready>() == ResultStatus::ok);
	SIM_EXPECT(farHandle.Emit<Event::alarm>(FaultWithCode(0)) == ResultStatus::busy);
	StartSequencedSession(line, near, far, log, 900);
	SIM_EXPECT(farHandle.Emit<Event::alarm>(FaultWithCode(5)) == ResultStatus::ok);
	RunRound(line, near, far, log, true, 1000);
	RunRound(line, near, far, log, true, 2000);
	RunRound(line, near, far, log, true, 3000);

	SIM_EXPECT(faultCalls == 1);
	SIM_EXPECT(lastFaultCode == 4);
	SIM_EXPECT(readyCalls == 0);
	SIM_EXPECT(farHandle.Refused() == 1);
	SIM_EXPECT(nearHandle.Refused() == 1);
	SIM_EXPECT(alarmCalls[5] == 0);
}


// A request that the ticket could not take, with no room in the refusal ring, is refused and counted in refused.
void TestRequestWithNoRoomIsCountedInRefused() {
	ResetEventState();
	ReportingPacketLine line(VUB::Faults{});
	RoomStack near(line.a);
	RoomStack far(line.b);
	WireLog log;
	Establish(line, near, far, log);

	auto handle = near.Requests();
	auto refused = handle.Send<Command::stop, &OnStopFinal>(VUB::NoBody{});
	SIM_EXPECT(refused.Error() == ResultStatus::busy);
	StartSequencedSession(line, near, far, log, 900);
	auto firstSend = handle.Send<Command::stop, &OnStopFinal>(VUB::NoBody{});
	SIM_EXPECT(firstSend.IsOk());
	auto secondSend = handle.Send<Command::stop, &OnStopFinal>(VUB::NoBody{});
	SIM_EXPECT(secondSend.IsOk());
	RunRound(line, near, far, log, true, 1000);
	RunRound(line, near, far, log, true, 1001);
	RunRound(line, near, far, log, true, 1002);

	auto farHandle = far.Requests();
	SIM_EXPECT(stopCalls == 1);
	SIM_EXPECT(farHandle.Refused() == 1);
}


// A final that arrives after its request's deadline matches no open request: it is counted in lateReplies, and the handler ran at the deadline.
void TestFinalAfterTheDeadlineIsCountedAsLateReply() {
	ResetEventState();
	ReportingPacketLine line(VUB::Faults{});
	HoldStack near(line.a);
	HoldStack far(line.b);
	WireLog log;
	Establish(line, near, far, log);

	auto handle = near.Requests();
	auto refused = handle.Send<Command::stop, &OnStopFinal>(VUB::NoBody{});
	SIM_EXPECT(refused.Error() == ResultStatus::busy);
	StartSequencedSession(line, near, far, log, 900);
	auto sent = handle.Send<Command::stop, &OnStopFinal>(VUB::NoBody{});
	SIM_EXPECT(sent.IsOk());
	// The timeouts are 2000 ms, so both deadlines (the requester's and the ticket's) are at 2001000 us.
	RunRound(line, near, far, log, true, 1000);
	RunRound(line, near, far, log, true, 2000999);
	SIM_EXPECT(stopFinals == 0);

	// The requester's deadline ends the request first, then the ticket's timeout final arrives with no open request.
	RunRound(line, near, far, log, true, 2001000);
	RunRound(line, near, far, log, true, 2001001);
	SIM_EXPECT(stopCalls == 1);
	SIM_EXPECT(stopFinals == 1);
	SIM_EXPECT(handle.LateReplies() == 1);
}


// The events snippet of Layer/Requests.md, compiled and run: a device emits three events, and its PC handles two of them.
namespace WikiEvents {

enum class Event : uint8 {
	fault = 0x40,
	ready = 0x41,
	alarm = 0x42
};


using DeviceEvents = VUB::CommandSet<+[]() {
	return VUB::Commands()
		.Event<Event::fault>()
			.Body<Motor::MotorFault>()
		.Event<Event::ready>()
			.Body<VUB::NoBody>()
		.Event<Event::alarm>()
			.Body<Motor::MotorFault>()
			.Delivered();
}>;


void OnFault(const Motor::MotorFault& fault) {
	(void)fault;
}


void OnReady() {
}


using PcHandlers = VUB::Handlers<
	VUB::OnEvent<Event::fault, &OnFault>,
	VUB::OnEvent<Event::ready, &OnReady>>;


// The device emits: an unacknowledged event, an event with no body, and a Delivered event.
template<typename StackT>
void EmitFromDevice(StackT& stack) {
	auto handle = stack.Requests();
	(void)handle.template Emit<Event::fault>(Motor::MotorFault{3});
	(void)handle.template Emit<Event::ready>();
	(void)handle.template Emit<Event::alarm>(Motor::MotorFault{5});
}

}


using WikiEventStack = VUB::Stack<+[]() {
	return VUB::Builder()
		.CountDrops()
		.SendQueue(4)
		.Layer<VUB::Requests<WikiEvents::DeviceEvents, WikiEvents::PcHandlers>>().Counted()
		.Layer<EventReliable>().Retries(3).TimeoutMs(200).Window(16)
		.Link<ReportingPacketEnd>()
		.MaxPayload(40)
		.ReceiveQueue(64)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


void TestWikiEventsSnippetCompilesAndRuns() {
	ResetEventState();
	ReportingPacketLine line(VUB::Faults{});
	WikiEventStack near(line.a);
	EventReceiver far(line.b);
	WireLog log;
	Establish(line, near, far, log);
	WikiEvents::EmitFromDevice(near);
	RunRound(line, near, far, log, true, 1000);
	SIM_EXPECT(faultCalls == 1);
	SIM_EXPECT(readyCalls == 1);
}


int main() {
	TestEventsOnPlainLinkAreExactBytes();
	TestWikiEventsSnippetCompilesAndRuns();
	TestCommandAndEventWithOneValueBothRun();
	TestEventBetweenTwoRequestsOfOneBurstRunsInOrder();
	TestDuplicateDeliveredEventIsHandledOnce();
	TestDeliveredEventSequenceWrapsAndEveryEventArrives();
	TestRefusedCounterStopsAtTheMaximum();
	TestTwoEventSetsOneForEachDirection();
	TestRequestWithNoRoomIsCountedInRefused();
	TestFinalAfterTheDeadlineIsCountedAsLateReply();
	TestNoBodyEventEmitsWithoutArgument();
	TestUnacknowledgedEventIsOnePacketWithNoAcknowledgement();
	TestUnacknowledgedEventIsNotResentAfterLoss();
	TestEventHandlersWithAndWithoutBodyBothRun();
	TestEventsOfOneBurstRunInArrivalOrder();
	TestFireAndEventOfOneBurstRunInArrivalOrder();
	TestRequestAndEventOfOneBurstRunInArrivalOrder();
	TestNewerEventBodyIsReadAsFarAsTheReceiverKnows();
	TestShortEventBodyIsNotDeliveredAndCountedAsLayerError();
	TestDeliveredEventAcknowledgedIsDeliveredOnceAndNotCounted();
	TestDeliveredEventLostOnceIsResentAndDeliveredOnce();
	TestDeliveredEventsEachDeliveredOnceUnderLossAndDuplication();
	TestDeliveredEventRetryExhaustedIsCountedInEventsLost();
	TestDeliveredEventPendingAtPeerRebootIsCountedAsReset();
	TestUnknownEventIdIsNotDeliveredAndIsRefused();
	TestEventWithoutHandlerIsRefusedAndNotDelivered();
	TestEmitUnderFailWhileLinkDownReturnsNoConnectionAndQueuesNothing();
	TestEmitUnderWaitWhileLinkDownGoesOutWhenLinkReturns();
	TestEmitWhenQueueIsFullReturnsBusyAndLosesNothing();
	return SimFinish("TestVubRequestsEvents");
}
