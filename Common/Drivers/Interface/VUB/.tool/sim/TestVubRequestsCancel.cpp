#include "ReliableChains.h"
#include "RequestsMotor.h"
#include "SimCheck.h"
#include <Drivers/Interface/VUB/Requests.h>
#include <Utilities/DataTypes.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <utility>

// Cancel of the requests layer over a reporting packet line: a requester (near, end a) and a responder (far, end b). Each test starts from
// zero. With Reliable under the layer a cancel is held until Reliable acknowledges its REQUEST; without Reliable it goes at once. The
// clock is testNowUs of Chains.h, in microseconds; the times of this file are in milliseconds, and Step converts them (the deadlines of
// the commands and the Reliable timeout are milliseconds too). Packets are checked by their last bytes, after Reliable's header when there
// is one.


// The command ids of the cancel tests. The value is the id on the wire.
enum class Command : uint8 {
	start = 0x12,
	stop = 0x13
};


// The commands of the cancel tests. start is cancellable and sends interim replies. stop is not cancellable; it has interim replies so that the
// cancel-refused progress can reach the requester.
using CancelCommands = VUB::CommandSet<+[]() {
	return VUB::Commands()
		.Command<Command::start>()
			.Request<Motor::MotorStart>()
			.Reply<Motor::MotorStatus, Motor::MotorReport>()
			.Interim()
			.Cancellable()
			.ReplyTimeoutMs(5000)
			.TicketTimeoutMs(60000)
		.Command<Command::stop>()
			.Reply<Motor::MotorStatus>()
			.Interim()
			.ReplyTimeoutMs(2000)
			.TicketTimeoutMs(2000);
}>;


using StartTicket = VUB::Ticket<CancelCommands, Command::start>;
using StopTicket = VUB::Ticket<CancelCommands, Command::stop>;


// The session value of the restarted requester in the restart test. A new boot gives a new value.
inline constexpr uint16 RestartedSession = 0x2345;


// What the device side does: the start and stop tickets it was given, and what each handler saw.
inline bool startAtOnce = false;
inline bool stopAtOnce = false;
inline bool cancelFinishes = false;
inline std::size_t startsSeen = 0;
inline std::size_t stopsSeen = 0;
inline std::size_t cancelRuns = 0;
inline std::array<StartTicket, 2> startTickets{};
inline std::size_t startTicketCount = 0;
inline StopTicket stopTicket;


// The requester's results, recorded by each handler.
inline std::size_t startFinals = 0;
inline std::size_t startCancelledFinals = 0;
inline ResultStatus startResult = ResultStatus::ok;
inline Motor::MotorStatus startStatus = Motor::MotorStatus::started;
inline Motor::MotorReport startReport{};
inline std::size_t startInterims = 0;
inline std::size_t stopFinals = 0;
inline ResultStatus stopResult = ResultStatus::ok;
inline std::size_t stopInterims = 0;
inline ResultStatus stopProgressResult = ResultStatus::ok;


Motor::MotorStart StartBody() {
	Motor::MotorStart body{};
	body.speedRpm = 1200;
	body.direction = 1;
	return body;
}


void OnStart(StartTicket ticket, const Motor::MotorStart& body) {
	(void)body;
	startsSeen++;
	if (startTicketCount < startTickets.size()) {
		startTickets[startTicketCount] = ticket;
	}

	startTicketCount++;
	if (startAtOnce) {
		Motor::MotorReport report{};
		report.speedRpm = 1200;
		report.currentMa = 850;
		(void)ticket.Finish(Motor::MotorStatus::started, report);
	}
}


// The cancel handler. If it finishes the ticket, the ticket's final is operationCancelled; if not, the layer sends that final.
void OnStartCancel(StartTicket ticket) {
	cancelRuns++;
	if (cancelFinishes) {
		(void)ticket.Cancelled();
	}
}


void OnStop(StopTicket ticket) {
	stopsSeen++;
	stopTicket = ticket;
	if (stopAtOnce) {
		(void)ticket.Finish(Motor::MotorStatus::started);
	}
}


using CancelDeviceHandlers = VUB::Handlers<
	VUB::On<Command::start, &OnStart>,
	VUB::OnCancel<Command::start, &OnStartCancel>,
	VUB::On<Command::stop, &OnStop>>;


void OnStartFinal(const VUB::Final<Motor::MotorStatus, Motor::MotorReport>& reply) {
	startFinals++;
	startResult = reply.result;
	startStatus = reply.status;
	startReport = reply.body;
	if (reply.result == ResultStatus::operationCancelled) {
		startCancelledFinals++;
	}
}


void OnStartInterim(const VUB::Progress<Motor::MotorStatus>& progress) {
	(void)progress;
	startInterims++;
}


void OnStopFinal(const VUB::Final<Motor::MotorStatus, VUB::NoBody>& reply) {
	stopFinals++;
	stopResult = reply.result;
}


void OnStopInterim(const VUB::Progress<Motor::MotorStatus>& progress) {
	stopInterims++;
	stopProgressResult = progress.result;
}


// The requester: four slots, the Reliable layer under it, and the reporting line's end.
using CancelRequester = VUB::Stack<+[]() {
	return VUB::Builder()
		.Layer<VUB::Requests<CancelCommands, VUB::Handlers<>>>().Open(4)
		.Layer<VUB::ReliableOf<40, 4>>().Retries(3).TimeoutMs(200).Window(4)
		.Link<ReportingPacketEnd>()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


// The responder: two tickets, the same Reliable layer.
using CancelResponder = VUB::Stack<+[]() {
	return VUB::Builder()
		.Layer<VUB::Requests<CancelCommands, CancelDeviceHandlers>>().Tickets(2)
		.Layer<VUB::ReliableOf<40, 4>>().Retries(3).TimeoutMs(200).Window(4)
		.Link<ReportingPacketEnd>()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


// The same requester and responder with no Reliable under the layer.
using PlainCancelRequester = VUB::Stack<+[]() {
	return VUB::Builder()
		.Layer<VUB::Requests<CancelCommands, VUB::Handlers<>>>().Open(4)
		.Link<ReportingPacketEnd>()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


using PlainCancelResponder = VUB::Stack<+[]() {
	return VUB::Builder()
		.Layer<VUB::Requests<CancelCommands, CancelDeviceHandlers>>().Tickets(2)
		.Link<ReportingPacketEnd>()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


// The responder with three tickets, for the CancelAll test with three requests open at once.
using CancelResponderThree = VUB::Stack<+[]() {
	return VUB::Builder()
		.Layer<VUB::Requests<CancelCommands, CancelDeviceHandlers>>().Tickets(3)
		.Layer<VUB::ReliableOf<40, 4>>().Retries(3).TimeoutMs(200).Window(4)
		.Link<ReportingPacketEnd>()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


using CancelHandle = decltype(std::declval<CancelRequester&>().Requests());
using PlainCancelHandle = decltype(std::declval<PlainCancelRequester&>().Requests());


// The packets of one round, by direction: toFar carries the requester's packets, toNear the responder's.
struct WireLog {
	ReliableCapture toFar;
	ReliableCapture toNear;


	void Clear() {
		toFar = ReliableCapture{};
		toNear = ReliableCapture{};
	}
};


// A rule that loses the next packets ending with its tail, on one direction. remaining counts the packets still to lose.
struct LossRule {
	std::array<uint8, 5> tail{};
	std::size_t tailLength = 0;
	std::size_t remaining = 0;
};


inline LossRule lossToFar;
inline LossRule lossToNear;


void LoseNext(LossRule& rule, std::span<const uint8> tail, std::size_t count) {
	rule.tailLength = std::min(tail.size(), rule.tail.size());
	for (std::size_t index = 0; index < rule.tailLength; index++) {
		rule.tail[index] = tail[index];
	}

	rule.remaining = count;
}


// True when the packet is lost by the rule: it ends with the tail and the rule still has packets to lose.
bool IsLost(LossRule& rule, std::span<const uint8> packet) {
	if (rule.remaining == 0 || packet.size() < rule.tailLength) {
		return false;
	}

	std::size_t start = packet.size() - rule.tailLength;
	for (std::size_t index = 0; index < rule.tailLength; index++) {
		if (packet[start + index] != rule.tail[index]) {
			return false;
		}
	}

	rule.remaining--;
	return true;
}


// Keeps a copy of each packet it is given, then passes it to the stack on the far end unless the loss rule takes it.
template<typename Stack>
struct Gate {
	ReliableCapture* capture;
	Stack* target;
	LossRule* loss;


	void Feed(uint8) {
	}


	void FeedPacket(std::span<const uint8> packet) {
		capture->FeedPacket(packet);
		if (IsLost(*loss, packet) == false) {
			target->FeedPacket(packet);
		}
	}
};


// One round: the requester runs, its packets reach the responder, the responder runs, its packets reach the requester, the requester runs.
// The time is in milliseconds.
template<typename NearStack, typename FarStack>
void Step(ReportingPacketLine& line, NearStack& near, FarStack& far, WireLog& log, uint32 nowMs) {
	testNowUs = nowMs * 1000u;
	log.Clear();
	near.Process();
	Gate<FarStack> forward{&log.toFar, &far, &lossToFar};
	line.DeliverToB(forward);
	far.Process();
	Gate<NearStack> back{&log.toNear, &near, &lossToNear};
	line.DeliverToA(back);
	near.Process();
}


// True when the capture holds a packet that ends with the tail, counted over all its packets.
template<std::size_t Size>
std::size_t CountEndingWith(const ReliableCapture& capture, const std::array<uint8, Size>& tail) {
	std::size_t found = 0;
	std::size_t limit = capture.count < capture.packets.size() ? capture.count : capture.packets.size();
	for (std::size_t index = 0; index < limit; index++) {
		std::size_t length = capture.lengths[index];
		if (length < Size) {
			continue;
		}

		std::size_t start = length - Size;
		bool same = true;
		for (std::size_t byte = 0; byte < Size && same; byte++) {
			same = capture.packets[index][start + byte] == tail[byte];
		}

		if (same) {
			found++;
		}
	}

	return found;
}


// The CANCEL packets of the capture for one number, as their last four bytes: kind 0x05, the number, and the tag. Number 0 is never a
// number on the wire, so CountCancelsOfZero counts those.
std::size_t CountCancels(const ReliableCapture& capture, uint16 number) {
	std::size_t found = 0;
	std::size_t limit = capture.count < capture.packets.size() ? capture.count : capture.packets.size();
	uint8 low = static_cast<uint8>(number & 0xFF);
	uint8 high = static_cast<uint8>(number >> 8);
	for (std::size_t index = 0; index < limit; index++) {
		std::size_t length = capture.lengths[index];
		if (length < 4) {
			continue;
		}

		std::size_t start = length - 4;
		const std::array<uint8, 64>& packet = capture.packets[index];
		bool isCancel = packet[start] == 0x05;
		isCancel = isCancel && packet[start + 1] == low;
		isCancel = isCancel && packet[start + 2] == high;
		isCancel = isCancel && packet[start + 3] == low;

		if (isCancel) {
			found++;
		}
	}

	return found;
}


// The CANCEL packets of the capture with number 0, whatever their tag. A CANCEL of number 0 is not sent by the layer, so none is expected.
std::size_t CountCancelsOfZero(const ReliableCapture& capture) {
	std::size_t found = 0;
	std::size_t limit = capture.count < capture.packets.size() ? capture.count : capture.packets.size();
	for (std::size_t index = 0; index < limit; index++) {
		std::size_t length = capture.lengths[index];
		if (length < 4) {
			continue;
		}

		std::size_t start = length - 4;
		const std::array<uint8, 64>& packet = capture.packets[index];
		bool isCancel = packet[start] == 0x05;
		isCancel = isCancel && packet[start + 1] == 0;
		isCancel = isCancel && packet[start + 2] == 0;
		if (isCancel) {
			found++;
		}
	}

	return found;
}


// Sends a start request with StartBody and returns its number, or 0 when the send is refused.
template<typename HandleType>
uint16 SendStart(HandleType& handle) {
	Result<VUB::RequestId> sent = handle.template Send<Command::start, &OnStartFinal, &OnStartInterim>(StartBody());
	if (sent.IsErr()) {
		return 0;
	}

	return sent.Value().number;
}


// Sends a stop request and returns its number, or 0 when the send is refused.
template<typename HandleType>
uint16 SendStop(HandleType& handle) {
	Result<VUB::RequestId> sent = handle.template Send<Command::stop, &OnStopFinal, &OnStopInterim>(VUB::NoBody{});
	if (sent.IsErr()) {
		return 0;
	}

	return sent.Value().number;
}


// Cancels the start with the number. The result is the layer's own.
template<typename HandleType>
ResultStatus CancelStart(HandleType& handle, uint16 number) {
	VUB::RequestId id{number};
	return handle.template Cancel<Command::start>(id);
}


// Sends a raw CANCEL packet from the requester (kind 0x05, the number, the tag), as a peer that is not this layer would.
ResultStatus InjectCancel(CancelRequester& near, uint16 number, uint8 tag) {
	std::array<uint8, 4> bytes = {0x05, static_cast<uint8>(number & 0xFF), static_cast<uint8>(number >> 8), tag};
	return near.LayerOutbox<0>().Send(std::span<const uint8>(bytes.data(), bytes.size()));
}


// The sessions of both ends, as the requests tests give them. Each end's first send is busy until the sync is acknowledged. The rounds end
// at 900 ms: Reliable times a packet from the last Process, so the tests send right after this and their first Process follows at once.
// A stream line carries bytes into a stack through Feed. The sink gives them to the stack of its end.
template<typename Stack>
struct StreamFeed {
	Stack* target;


	void Feed(uint8 value) {
		target->Feed(value);
	}
};


// One round over a stream line: each end takes at most its byte budget of the round (no budget is unlimited), so a frame can be left part
// written. The times are in milliseconds, as Step's.
template<typename NearStack, typename FarStack>
void StreamRound(ReportingStreamLine& line, NearStack& near, FarStack& far, uint32 nowMs, std::size_t nearBytes, std::size_t farBytes) {
	testNowUs = nowMs * 1000u;
	line.a.StartRound(nearBytes);
	line.b.StartRound(farBytes);
	near.Process();
	StreamFeed<FarStack> forward{&far};
	line.DeliverToB(forward);
	far.Process();
	StreamFeed<NearStack> back{&near};
	line.DeliverToA(back);
	near.Process();
}


const std::size_t UnlimitedBytes = static_cast<std::size_t>(-1);


// A round on either kind of line, as the tests of the layer run them.
template<typename NearStack, typename FarStack>
void Round(ReportingPacketLine& line, NearStack& near, FarStack& far, WireLog& log, uint32 nowMs) {
	Step(line, near, far, log, nowMs);
}


template<typename NearStack, typename FarStack>
void Round(ReportingStreamLine& line, NearStack& near, FarStack& far, WireLog& log, uint32 nowMs) {
	(void)log;
	StreamRound(line, near, far, nowMs, UnlimitedBytes, UnlimitedBytes);
}


template<typename Line, typename NearStack, typename FarStack>
void Establish(Line& line, NearStack& near, FarStack& far, WireLog& log) {
	GiveSession(near);
	GiveSession(far);
	Round(line, near, far, log, 50);
	auto handle = near.Requests();
	Result<VUB::RequestId> probe = handle.template Send<Command::stop, &OnStopFinal, &OnStopInterim>(VUB::NoBody{});
	SIM_EXPECT(probe.IsErr());
	SIM_EXPECT(probe.Error() == ResultStatus::busy);
	std::array<uint8, 1> trigger = {0xEE};
	SIM_EXPECT(far.template LayerOutbox<0>().Send(std::span<const uint8>(trigger.data(), trigger.size())) == ResultStatus::busy);
	for (uint32 round = 0; round < 3; round++) {
		Round(line, near, far, log, 900 + round);
	}
}


void ResetCancelState() {
	ResetReliableState();
	startAtOnce = false;
	stopAtOnce = false;
	cancelFinishes = false;
	startsSeen = 0;
	stopsSeen = 0;
	cancelRuns = 0;
	startTickets = {};
	startTicketCount = 0;
	stopTicket = StopTicket{};
	startFinals = 0;
	startCancelledFinals = 0;
	startResult = ResultStatus::ok;
	startStatus = Motor::MotorStatus::started;
	startReport = Motor::MotorReport{};
	startInterims = 0;
	stopFinals = 0;
	stopResult = ResultStatus::ok;
	stopInterims = 0;
	stopProgressResult = ResultStatus::ok;
	lossToFar = LossRule{};
	lossToNear = LossRule{};
}


// The REQUEST of start for number 1, tag 1: id 0x12, speed 1200, direction 1, the default ramp 300.
inline std::array<uint8, 10> StartRequestOne = {0x01, 0x12, 0x01, 0x00, 0x01, 0xB0, 0x04, 0x01, 0x2C, 0x01};

// The last five bytes of that REQUEST: a loss rule can name them.
inline std::array<uint8, 5> StartRequestOneTail = {0xB0, 0x04, 0x01, 0x2C, 0x01};

// The CANCEL of number 1, tag 1, and the final operationCancelled of number 1, tag 1.
inline std::array<uint8, 4> CancelOne = {0x05, 0x01, 0x00, 0x01};
inline std::array<uint8, 5> CancelledFinalOne = {0x03, 0x01, 0x00, 0x01, 0x01};
inline std::array<uint8, 5> TimeoutFinalOne = {0x03, 0x01, 0x00, 0x01, 0x05};

// Without Reliable the requester's session is SetSession(PlainSession): the first number is the low 16 bits of value x 0x9E37, and the first tag
// is the low byte of value. The arithmetic: 30599 x 0x9E37 (40503) = 1239351297 = 18911 x 65536 + 1, so the low 16 bits are 1 and the first
// number is 1. 30599 = 119 x 256 + 135, so the first tag is 135 (0x87). The packets are those above, with tag 0x87.
inline constexpr uint16 PlainSession = 30599;
inline std::array<uint8, 10> PlainStartRequestOne = {0x01, 0x12, 0x01, 0x00, 0x87, 0xB0, 0x04, 0x01, 0x2C, 0x01};
inline std::array<uint8, 4> PlainCancelOne = {0x05, 0x01, 0x00, 0x87};
inline std::array<uint8, 5> PlainCancelledFinalOne = {0x03, 0x01, 0x00, 0x87, 0x01};


// The CANCEL is held until the REQUEST is acknowledged: in the round the request goes out no CANCEL is sent. Then the device ends the
// request with operationCancelled.
void TestCancelIsHeldUntilRequestIsAcknowledged() {
	ResetCancelState();
	ReportingPacketLine line(VUB::Faults{});
	CancelRequester near(line.a);
	CancelResponder far(line.b);
	WireLog log;
	Establish(line, near, far, log);
	CancelHandle handle = near.Requests();
	uint16 number = SendStart(handle);
	SIM_EXPECT(number == 1);
	SIM_EXPECT(CancelStart(handle, number) == ResultStatus::ok);
	Step(line, near, far, log, 1000);
	SIM_EXPECT(startsSeen == 1);
	SIM_EXPECT(CountEndingWith(log.toFar, StartRequestOne) == 1);
	SIM_EXPECT(CountCancels(log.toFar, 1) == 0);
	std::size_t cancelsOnWire = 0;
	for (uint32 now = 1100; now <= 1400; now += 100) {
		Step(line, near, far, log, now);
		cancelsOnWire += CountCancels(log.toFar, 1);
	}

	SIM_EXPECT(cancelsOnWire == 1);
	SIM_EXPECT(cancelRuns == 1);
	SIM_EXPECT(startFinals == 1);
	SIM_EXPECT(startResult == ResultStatus::operationCancelled);
	SIM_EXPECT(startInterims == 0);
}


// Without Reliable the order is kept and nothing holds the cancel: the CANCEL goes in the same round as its REQUEST, right after it.
void TestCancelGoesAtOnceWithoutReliable() {
	ResetCancelState();
	ReportingPacketLine line(VUB::Faults{});
	PlainCancelRequester near(line.a);
	PlainCancelResponder far(line.b);
	WireLog log;
	Step(line, near, far, log, 50);
	PlainCancelHandle handle = near.Requests();
	handle.SetSession(PlainSession);
	uint16 number = SendStart(handle);
	SIM_EXPECT(number == 1);
	SIM_EXPECT(CancelStart(handle, number) == ResultStatus::ok);
	Step(line, near, far, log, 1000);
	SIM_EXPECT(log.toFar.count == 2);
	SIM_EXPECT(CaptureIs(log.toFar, 0, PlainStartRequestOne));
	SIM_EXPECT(CaptureIs(log.toFar, 1, PlainCancelOne));
}


// The REQUEST and its CANCEL arrive in one burst: the device never runs its start handler, and the final is operationCancelled.
void TestRequestAndCancelInOneBurstRunNoHandler() {
	ResetCancelState();
	ReportingPacketLine line(VUB::Faults{});
	PlainCancelRequester near(line.a);
	PlainCancelResponder far(line.b);
	WireLog log;
	Step(line, near, far, log, 50);
	PlainCancelHandle handle = near.Requests();
	handle.SetSession(PlainSession);
	uint16 number = SendStart(handle);
	SIM_EXPECT(number == 1);
	SIM_EXPECT(CancelStart(handle, number) == ResultStatus::ok);
	Step(line, near, far, log, 1000);
	SIM_EXPECT(startsSeen == 0);
	SIM_EXPECT(cancelRuns == 0);
	SIM_EXPECT(startFinals == 1);
	SIM_EXPECT(startResult == ResultStatus::operationCancelled);
	SIM_EXPECT(log.toNear.count == 1);
	SIM_EXPECT(CaptureIs(log.toNear, 0, PlainCancelledFinalOne));
}


// The cancel handler finishes its ticket with Cancelled(): the final is operationCancelled, the ticket is closed, and a later Finish is
// notAvailable.
void TestCancelHandlerFinishesTicket() {
	ResetCancelState();
	cancelFinishes = true;
	ReportingPacketLine line(VUB::Faults{});
	CancelRequester near(line.a);
	CancelResponder far(line.b);
	WireLog log;
	Establish(line, near, far, log);
	CancelHandle handle = near.Requests();
	uint16 number = SendStart(handle);
	SIM_EXPECT(number == 1);
	SIM_EXPECT(CancelStart(handle, number) == ResultStatus::ok);
	for (uint32 now = 1000; now <= 1400; now += 100) {
		Step(line, near, far, log, now);
	}

	SIM_EXPECT(cancelRuns == 1);
	SIM_EXPECT(startFinals == 1);
	SIM_EXPECT(startResult == ResultStatus::operationCancelled);
	SIM_EXPECT(startTickets[0].Open() == false);
	Motor::MotorReport report{};
	SIM_EXPECT(startTickets[0].Finish(Motor::MotorStatus::started, report) == ResultStatus::notAvailable);
}


// The cancel handler returns with its ticket still open: the layer sends operationCancelled for it, so the requester is not left waiting.
// The ticket is then closed and a later Finish is notAvailable, and the handler does not run again.
void TestCancelLeftUnfinishedGetsOperationCancelled() {
	ResetCancelState();
	ReportingPacketLine line(VUB::Faults{});
	CancelRequester near(line.a);
	CancelResponder far(line.b);
	WireLog log;
	Establish(line, near, far, log);
	CancelHandle handle = near.Requests();
	uint16 number = SendStart(handle);
	SIM_EXPECT(number == 1);
	SIM_EXPECT(CancelStart(handle, number) == ResultStatus::ok);
	std::size_t finalsOnWire = 0;
	for (uint32 now = 1000; now <= 1400; now += 100) {
		Step(line, near, far, log, now);
		finalsOnWire += CountEndingWith(log.toNear, CancelledFinalOne);
	}

	SIM_EXPECT(finalsOnWire == 1);
	SIM_EXPECT(cancelRuns == 1);
	SIM_EXPECT(startTickets[0].Open() == false);
	Motor::MotorReport report{};
	SIM_EXPECT(startTickets[0].Finish(Motor::MotorStatus::started, report) == ResultStatus::notAvailable);
	for (uint32 now = 1500; now <= 1700; now += 100) {
		Step(line, near, far, log, now);
	}

	SIM_EXPECT(cancelRuns == 1);
}


// The REQUEST is answered at once, so its final carries the acknowledgement. The held CANCEL is dropped: it never goes out, and the
// device's cancel handler never runs.
void TestFinalBeforeAcknowledgementDropsHeldCancel() {
	ResetCancelState();
	startAtOnce = true;
	ReportingPacketLine line(VUB::Faults{});
	CancelRequester near(line.a);
	CancelResponder far(line.b);
	WireLog log;
	Establish(line, near, far, log);
	CancelHandle handle = near.Requests();
	uint16 number = SendStart(handle);
	SIM_EXPECT(number == 1);
	SIM_EXPECT(CancelStart(handle, number) == ResultStatus::ok);
	Step(line, near, far, log, 1000);
	SIM_EXPECT(startFinals == 1);
	SIM_EXPECT(startResult == ResultStatus::ok);
	std::size_t cancelsOnWire = 0;
	for (uint32 now = 1100; now <= 1300; now += 100) {
		Step(line, near, far, log, now);
		cancelsOnWire += CountCancels(log.toFar, 1);
	}

	SIM_EXPECT(cancelsOnWire == 0);
	SIM_EXPECT(cancelRuns == 0);
	SIM_EXPECT(startFinals == 1);
}


// The cancel and the device's own final cross. The device finishes first, so its final is on the way when the CANCEL reaches it: the
// CANCEL is dropped, the handler does not run, and the requester gets the real result.
void TestResponderFinalFirstWinsOverCancel() {
	ResetCancelState();
	ReportingPacketLine line(VUB::Faults{});
	CancelRequester near(line.a);
	CancelResponder far(line.b);
	WireLog log;
	Establish(line, near, far, log);
	CancelHandle handle = near.Requests();
	uint16 number = SendStart(handle);
	SIM_EXPECT(number == 1);
	Step(line, near, far, log, 1000);
	Step(line, near, far, log, 1100);
	SIM_EXPECT(startsSeen == 1);
	SIM_EXPECT(startFinals == 0);
	SIM_EXPECT(CancelStart(handle, number) == ResultStatus::ok);
	Motor::MotorReport report{};
	report.speedRpm = 1200;
	report.currentMa = 850;
	SIM_EXPECT(startTickets[0].Finish(Motor::MotorStatus::started, report) == ResultStatus::ok);
	for (uint32 now = 1200; now <= 1400; now += 100) {
		Step(line, near, far, log, now);
	}

	SIM_EXPECT(startFinals == 1);
	SIM_EXPECT(startResult == ResultStatus::ok);
	SIM_EXPECT(startStatus == Motor::MotorStatus::started);
	SIM_EXPECT(startReport.speedRpm == 1200);
	SIM_EXPECT(startReport.currentMa == 850);
	SIM_EXPECT(cancelRuns == 0);
	SIM_EXPECT(startTickets[0].Open() == false);
}


// The requester's final has closed the request: a cancel of its number finds nothing open, and nothing is sent.
void TestCancelAfterFinalIsNotFound() {
	ResetCancelState();
	startAtOnce = true;
	ReportingPacketLine line(VUB::Faults{});
	CancelRequester near(line.a);
	CancelResponder far(line.b);
	WireLog log;
	Establish(line, near, far, log);
	CancelHandle handle = near.Requests();
	uint16 number = SendStart(handle);
	SIM_EXPECT(number == 1);
	Step(line, near, far, log, 1000);
	SIM_EXPECT(startFinals == 1);
	SIM_EXPECT(CancelStart(handle, number) == ResultStatus::notFound);
	std::size_t cancelsOnWire = 0;
	for (uint32 now = 1100; now <= 1300; now += 100) {
		Step(line, near, far, log, now);
		cancelsOnWire += CountCancels(log.toFar, 1);
	}

	SIM_EXPECT(cancelsOnWire == 0);
	SIM_EXPECT(cancelRuns == 0);
	SIM_EXPECT(startFinals == 1);
}


// A CANCEL for a number that is not open, and one for the open number with another tag, are dropped without a reply: the open request
// keeps its ticket. A CANCEL for the number of a closed request is dropped the same way.
void TestCancelForUnknownNumberOrTagIsDropped() {
	ResetCancelState();
	ReportingPacketLine line(VUB::Faults{});
	CancelRequester near(line.a);
	CancelResponder far(line.b);
	WireLog log;
	Establish(line, near, far, log);
	CancelHandle handle = near.Requests();
	uint16 number = SendStart(handle);
	SIM_EXPECT(number == 1);
	Step(line, near, far, log, 1000);
	Step(line, near, far, log, 1100);
	SIM_EXPECT(startsSeen == 1);
	SIM_EXPECT(InjectCancel(near, 99, 99) == ResultStatus::ok);
	SIM_EXPECT(InjectCancel(near, 1, 9) == ResultStatus::ok);
	Step(line, near, far, log, 1200);
	Step(line, near, far, log, 1300);
	SIM_EXPECT(cancelRuns == 0);
	SIM_EXPECT(startFinals == 0);
	SIM_EXPECT(startInterims == 0);
	SIM_EXPECT(startTickets[0].Open());
	Motor::MotorReport report{};
	SIM_EXPECT(startTickets[0].Finish(Motor::MotorStatus::started, report) == ResultStatus::ok);
	Step(line, near, far, log, 1400);
	SIM_EXPECT(startFinals == 1);
	SIM_EXPECT(startResult == ResultStatus::ok);
	SIM_EXPECT(InjectCancel(near, 1, 1) == ResultStatus::ok);
	Step(line, near, far, log, 1500);
	Step(line, near, far, log, 1600);
	SIM_EXPECT(cancelRuns == 0);
	SIM_EXPECT(startFinals == 1);
}


// A CANCEL of a command that is not cancellable gets an interim with the cancel-refused status 0xF8, and the request runs to its own end.
void TestCancelOfNotCancellableCommandGetsRefusedInterim() {
	ResetCancelState();
	ReportingPacketLine line(VUB::Faults{});
	CancelRequester near(line.a);
	CancelResponder far(line.b);
	WireLog log;
	Establish(line, near, far, log);
	CancelHandle handle = near.Requests();
	uint16 number = SendStop(handle);
	SIM_EXPECT(number == 1);
	Step(line, near, far, log, 1000);
	Step(line, near, far, log, 1100);
	SIM_EXPECT(stopsSeen == 1);
	SIM_EXPECT(InjectCancel(near, 1, 1) == ResultStatus::ok);
	std::array<uint8, 6> refused = {0x02, 0x01, 0x00, 0x01, 0x01, 0xF8};
	std::size_t refusedOnWire = 0;
	for (uint32 now = 1200; now <= 1300; now += 100) {
		Step(line, near, far, log, now);
		refusedOnWire += CountEndingWith(log.toNear, refused);
	}

	SIM_EXPECT(refusedOnWire == 1);
	SIM_EXPECT(stopInterims == 1);
	SIM_EXPECT(stopProgressResult == ResultStatus::notSupported);
	SIM_EXPECT(stopFinals == 0);
	SIM_EXPECT(stopTicket.Open());
	SIM_EXPECT(stopTicket.Finish(Motor::MotorStatus::started) == ResultStatus::ok);
	Step(line, near, far, log, 1400);
	Step(line, near, far, log, 1500);
	SIM_EXPECT(stopFinals == 1);
	SIM_EXPECT(stopResult == ResultStatus::ok);
}


// A second cancel of a held request changes nothing: one CANCEL goes out, and the device's cancel handler runs once.
void TestSecondCancelChangesNothing() {
	ResetCancelState();
	ReportingPacketLine line(VUB::Faults{});
	CancelRequester near(line.a);
	CancelResponder far(line.b);
	WireLog log;
	Establish(line, near, far, log);
	CancelHandle handle = near.Requests();
	uint16 number = SendStart(handle);
	SIM_EXPECT(number == 1);
	SIM_EXPECT(CancelStart(handle, number) == ResultStatus::ok);
	SIM_EXPECT(CancelStart(handle, number) == ResultStatus::ok);
	std::size_t cancelsOnWire = 0;
	for (uint32 now = 1000; now <= 1400; now += 100) {
		Step(line, near, far, log, now);
		cancelsOnWire += CountCancels(log.toFar, 1);
	}

	SIM_EXPECT(cancelsOnWire == 1);
	SIM_EXPECT(cancelRuns == 1);
	SIM_EXPECT(startFinals == 1);
	SIM_EXPECT(startResult == ResultStatus::operationCancelled);
}


// CancelAll cancels every open start: both tickets run the cancel handler, and both requests end with operationCancelled.
void TestCancelAllCancelsEveryOpenStart() {
	ResetCancelState();
	ReportingPacketLine line(VUB::Faults{});
	CancelRequester near(line.a);
	CancelResponder far(line.b);
	WireLog log;
	Establish(line, near, far, log);
	CancelHandle handle = near.Requests();
	SIM_EXPECT(SendStart(handle) == 1);
	SIM_EXPECT(SendStart(handle) == 2);
	Step(line, near, far, log, 1000);
	Step(line, near, far, log, 1100);
	SIM_EXPECT(startsSeen == 2);
	SIM_EXPECT(handle.CancelAll() == ResultStatus::ok);
	std::size_t cancelsOne = 0;
	std::size_t cancelsTwo = 0;
	std::size_t zeroCancels = 0;
	for (uint32 now = 1200; now <= 1500; now += 100) {
		Step(line, near, far, log, now);
		cancelsOne += CountCancels(log.toFar, 1);
		cancelsTwo += CountCancels(log.toFar, 2);
		zeroCancels += CountCancelsOfZero(log.toFar);
	}

	SIM_EXPECT(cancelsOne == 1);
	SIM_EXPECT(cancelsTwo == 1);
	SIM_EXPECT(zeroCancels == 0);
	SIM_EXPECT(cancelRuns == 2);
	SIM_EXPECT(startFinals == 2);
	SIM_EXPECT(startCancelledFinals == 2);
}


// A cancel of an id that is not open returns notFound and sends nothing; CancelAll with nothing open sends nothing.
void TestCancelOfUnknownIdAndCancelAllWithNothingOpenSendNothing() {
	ResetCancelState();
	ReportingPacketLine line(VUB::Faults{});
	CancelRequester near(line.a);
	CancelResponder far(line.b);
	WireLog log;
	Establish(line, near, far, log);
	CancelHandle handle = near.Requests();
	SIM_EXPECT(CancelStart(handle, 77) == ResultStatus::notFound);
	(void)handle.CancelAll();
	std::size_t cancelsOnWire = 0;
	for (uint32 now = 1000; now <= 1200; now += 100) {
		Step(line, near, far, log, now);
		cancelsOnWire += CountCancels(log.toFar, 77) + CountCancelsOfZero(log.toFar);
	}

	SIM_EXPECT(cancelsOnWire == 0);
	SIM_EXPECT(cancelRuns == 0);
	SIM_EXPECT(startsSeen == 0);
}


// The device's final is lost on its way back, after the CANCEL was delivered. The requester's deadline ends the request, and it reports
// operationCancelled because the cancel had been sent.
void TestDeadlineWhileCancellingGivesOperationCancelled() {
	ResetCancelState();
	ReportingPacketLine line(VUB::Faults{});
	CancelRequester near(line.a);
	CancelResponder far(line.b);
	WireLog log;
	Establish(line, near, far, log);
	CancelHandle handle = near.Requests();
	uint16 number = SendStart(handle);
	SIM_EXPECT(number == 1);
	Step(line, near, far, log, 1000);
	Step(line, near, far, log, 1100);
	SIM_EXPECT(CancelStart(handle, number) == ResultStatus::ok);
	LoseNext(lossToNear, CancelledFinalOne, 100);
	Step(line, near, far, log, 1200);
	SIM_EXPECT(cancelRuns == 1);
	SIM_EXPECT(startFinals == 0);
	for (uint32 now = 2000; now <= 7000; now += 1000) {
		Step(line, near, far, log, now);
	}

	SIM_EXPECT(cancelRuns == 1);
	SIM_EXPECT(startFinals == 1);
	SIM_EXPECT(startResult == ResultStatus::operationCancelled);
	SIM_EXPECT(startTickets[0].Open() == false);
}


// The first CANCEL is lost on the line. Reliable resends it, the device runs its cancel handler once, and the requester gets
// operationCancelled.
void TestLostCancelIsResentByReliable() {
	ResetCancelState();
	ReportingPacketLine line(VUB::Faults{});
	CancelRequester near(line.a);
	CancelResponder far(line.b);
	WireLog log;
	Establish(line, near, far, log);
	CancelHandle handle = near.Requests();
	uint16 number = SendStart(handle);
	SIM_EXPECT(number == 1);
	Step(line, near, far, log, 1000);
	Step(line, near, far, log, 1100);
	SIM_EXPECT(startsSeen == 1);
	LoseNext(lossToFar, CancelOne, 1);
	SIM_EXPECT(CancelStart(handle, number) == ResultStatus::ok);
	std::size_t cancelsOnWire = 0;
	for (uint32 now = 1200; now <= 1800; now += 100) {
		Step(line, near, far, log, now);
		cancelsOnWire += CountCancels(log.toFar, 1);
	}

	SIM_EXPECT(cancelsOnWire == 2);
	SIM_EXPECT(cancelRuns == 1);
	SIM_EXPECT(startFinals == 1);
	SIM_EXPECT(startResult == ResultStatus::operationCancelled);
}


// Without Reliable a lost CANCEL is not resent. The requester's deadline ends the request with operationCancelled, and the device still
// holds its ticket, since it never saw the cancel.
void TestLostCancelWithoutReliableEndsAtDeadline() {
	ResetCancelState();
	ReportingPacketLine line(VUB::Faults{});
	PlainCancelRequester near(line.a);
	PlainCancelResponder far(line.b);
	WireLog log;
	Step(line, near, far, log, 50);
	PlainCancelHandle handle = near.Requests();
	handle.SetSession(PlainSession);
	uint16 number = SendStart(handle);
	SIM_EXPECT(number == 1);
	Step(line, near, far, log, 1000);
	SIM_EXPECT(startsSeen == 1);
	LoseNext(lossToFar, PlainCancelOne, 100);
	SIM_EXPECT(CancelStart(handle, number) == ResultStatus::ok);
	for (uint32 now = 1100; now <= 7000; now += 500) {
		Step(line, near, far, log, now);
	}

	SIM_EXPECT(startFinals == 1);
	SIM_EXPECT(startResult == ResultStatus::operationCancelled);
	SIM_EXPECT(cancelRuns == 0);
	SIM_EXPECT(startTickets[0].Open());
}


// The link goes down while the CANCEL is held. Nothing reaches the peer while it is down. When the link comes back, the REQUEST is
// delivered once, its acknowledgement comes, and the held CANCEL goes out.
void TestCancelHeldWhileLinkIsDown() {
	ResetCancelState();
	ReportingPacketLine line(VUB::Faults{});
	CancelRequester near(line.a);
	CancelResponder far(line.b);
	WireLog log;
	Establish(line, near, far, log);
	CancelHandle handle = near.Requests();
	uint16 number = SendStart(handle);
	SIM_EXPECT(number == 1);
	SIM_EXPECT(CancelStart(handle, number) == ResultStatus::ok);
	line.SetLinkUp(false);
	std::size_t cancelsWhileDown = 0;
	for (uint32 now = 1000; now <= 2000; now += 500) {
		Step(line, near, far, log, now);
		cancelsWhileDown += CountCancels(log.toFar, 1);
	}

	SIM_EXPECT(cancelsWhileDown == 0);
	SIM_EXPECT(startsSeen == 0);
	line.SetLinkUp(true);
	std::size_t cancelsOnWire = 0;
	for (uint32 now = 2500; now <= 4000; now += 100) {
		Step(line, near, far, log, now);
		cancelsOnWire += CountCancels(log.toFar, 1);
	}

	SIM_EXPECT(cancelsOnWire == 1);
	SIM_EXPECT(startsSeen == 1);
	SIM_EXPECT(cancelRuns == 1);
	SIM_EXPECT(startFinals == 1);
	SIM_EXPECT(startResult == ResultStatus::operationCancelled);
}


// The device's ticket deadline ends a start nobody finishes. The cancel handler runs once, then the layer sends the final timeout (code 5),
// and a later Finish is notAvailable. The requester's own deadline is shorter, so it reports timeout.
void TestTicketDeadlineRunsCancelHandlerOnce() {
	ResetCancelState();
	ReportingPacketLine line(VUB::Faults{});
	CancelRequester near(line.a);
	CancelResponder far(line.b);
	WireLog log;
	Establish(line, near, far, log);
	CancelHandle handle = near.Requests();
	uint16 number = SendStart(handle);
	SIM_EXPECT(number == 1);
	Step(line, near, far, log, 1000);
	SIM_EXPECT(startsSeen == 1);
	SIM_EXPECT(cancelRuns == 0);
	std::size_t timeoutFinalsOnWire = 0;
	for (uint32 now = 2000; now <= 70000; now += 100) {
		Step(line, near, far, log, now);
		timeoutFinalsOnWire += CountEndingWith(log.toNear, TimeoutFinalOne);
	}

	SIM_EXPECT(cancelRuns == 1);
	SIM_EXPECT(timeoutFinalsOnWire == 1);
	SIM_EXPECT(startTickets[0].Open() == false);
	Motor::MotorReport report{};
	SIM_EXPECT(startTickets[0].Finish(Motor::MotorStatus::started, report) == ResultStatus::notAvailable);
	SIM_EXPECT(startResult == ResultStatus::timeout);
	SIM_EXPECT(startFinals == 1);
}


// The requester restarts and syncs with a new session. The device's open start ticket is closed by the new boot: its cancel handler runs
// once, and a later Finish is notAvailable.
void TestPeerRestartRunsCancelHandlerOnceAndClosesTicket() {
	ResetCancelState();
	ReportingPacketLine line(VUB::Faults{});
	std::optional<CancelRequester> near;
	near.emplace(line.a);
	CancelResponder far(line.b);
	WireLog log;
	Establish(line, *near, far, log);
	CancelHandle handle = near->Requests();
	uint16 number = SendStart(handle);
	SIM_EXPECT(number == 1);
	Step(line, *near, far, log, 1000);
	Step(line, *near, far, log, 1100);
	SIM_EXPECT(startsSeen == 1);
	near.reset();
	near.emplace(line.a);
	GiveSession(*near, RestartedSession);
	Step(line, *near, far, log, 1200);
	uint16 stopNumber = 0;
	for (uint32 now = 1300; now <= 1700 && stopNumber == 0; now += 100) {
		CancelHandle restartedHandle = near->Requests();
		stopNumber = SendStop(restartedHandle);
		Step(line, *near, far, log, now);
	}

	SIM_EXPECT(stopNumber != 0);
	SIM_EXPECT(cancelRuns == 1);
	SIM_EXPECT(startTickets[0].Open() == false);
	Motor::MotorReport report{};
	SIM_EXPECT(startTickets[0].Finish(Motor::MotorStatus::started, report) == ResultStatus::notAvailable);
	SIM_EXPECT(cancelRuns == 1);
}


// A ticket that no request has reached is closed: Open is false, and its calls return notAvailable (decision 9).
void TestDefaultTicketIsClosed() {
	StartTicket ticket;
	SIM_EXPECT(ticket.Open() == false);
	Motor::MotorReport report{};
	SIM_EXPECT(ticket.Finish(Motor::MotorStatus::started, report) == ResultStatus::notAvailable);
	SIM_EXPECT(ticket.Interim(Motor::MotorStatus::warming) == ResultStatus::notAvailable);
}


// A request sent after CancelAll is not cancelled: CancelAll reaches only the requests open when it is called (decision 11).
void TestRequestSentAfterCancelAllIsNotCancelled() {
	ResetCancelState();
	ReportingPacketLine line(VUB::Faults{});
	CancelRequester near(line.a);
	CancelResponder far(line.b);
	WireLog log;
	Establish(line, near, far, log);
	CancelHandle handle = near.Requests();
	SIM_EXPECT(SendStart(handle) == 1);
	Step(line, near, far, log, 1000);
	Step(line, near, far, log, 1100);
	SIM_EXPECT(startsSeen == 1);
	SIM_EXPECT(handle.CancelAll() == ResultStatus::ok);
	SIM_EXPECT(SendStart(handle) == 2);
	std::size_t cancelsOne = 0;
	std::size_t cancelsTwo = 0;
	for (uint32 now = 1200; now <= 1500; now += 100) {
		Step(line, near, far, log, now);
		cancelsOne += CountCancels(log.toFar, 1);
		cancelsTwo += CountCancels(log.toFar, 2);
	}

	SIM_EXPECT(cancelsOne == 1);
	SIM_EXPECT(cancelsTwo == 0);
	SIM_EXPECT(startsSeen == 2);
	SIM_EXPECT(cancelRuns == 1);
	SIM_EXPECT(startCancelledFinals == 1);
	SIM_EXPECT(startFinals == 1);
	Motor::MotorReport report{};
	SIM_EXPECT(startTickets[1].Finish(Motor::MotorStatus::started, report) == ResultStatus::ok);
	for (uint32 now = 1600; now <= 1800; now += 100) {
		Step(line, near, far, log, now);
	}

	SIM_EXPECT(cancelRuns == 1);
	SIM_EXPECT(startFinals == 2);
	SIM_EXPECT(startCancelledFinals == 1);
	SIM_EXPECT(startResult == ResultStatus::ok);
}


// CancelAll leaves a request of a command that is not cancellable open: only the cancellable start gets a CANCEL (decision 11).
void TestCancelAllLeavesNotCancellableRequestsOpen() {
	ResetCancelState();
	ReportingPacketLine line(VUB::Faults{});
	CancelRequester near(line.a);
	CancelResponder far(line.b);
	WireLog log;
	Establish(line, near, far, log);
	CancelHandle handle = near.Requests();
	SIM_EXPECT(SendStart(handle) == 1);
	SIM_EXPECT(SendStop(handle) == 2);
	Step(line, near, far, log, 1000);
	Step(line, near, far, log, 1100);
	SIM_EXPECT(startsSeen == 1);
	SIM_EXPECT(stopsSeen == 1);
	SIM_EXPECT(handle.CancelAll() == ResultStatus::ok);
	std::size_t cancelsOne = 0;
	std::size_t cancelsTwo = 0;
	for (uint32 now = 1200; now <= 1500; now += 100) {
		Step(line, near, far, log, now);
		cancelsOne += CountCancels(log.toFar, 1);
		cancelsTwo += CountCancels(log.toFar, 2);
	}

	SIM_EXPECT(cancelsOne == 1);
	SIM_EXPECT(cancelsTwo == 0);
	SIM_EXPECT(cancelRuns == 1);
	SIM_EXPECT(stopFinals == 0);
	SIM_EXPECT(stopTicket.Open());
	SIM_EXPECT(stopTicket.Finish(Motor::MotorStatus::started) == ResultStatus::ok);
	Step(line, near, far, log, 1600);
	Step(line, near, far, log, 1700);
	SIM_EXPECT(stopFinals == 1);
	SIM_EXPECT(stopResult == ResultStatus::ok);
}


// A CANCEL of number 0 is not a cancel of any request: the layer drops it, with no reply, and the open start keeps its ticket (decision 11).
void TestCancelOfNumberZeroIsDropped() {
	ResetCancelState();
	ReportingPacketLine line(VUB::Faults{});
	CancelRequester near(line.a);
	CancelResponder far(line.b);
	WireLog log;
	Establish(line, near, far, log);
	CancelHandle handle = near.Requests();
	uint16 number = SendStart(handle);
	SIM_EXPECT(number == 1);
	Step(line, near, far, log, 1000);
	Step(line, near, far, log, 1100);
	SIM_EXPECT(startsSeen == 1);
	SIM_EXPECT(InjectCancel(near, 0, 1) == ResultStatus::ok);
	Step(line, near, far, log, 1200);
	Step(line, near, far, log, 1300);
	SIM_EXPECT(cancelRuns == 0);
	SIM_EXPECT(startFinals == 0);
	SIM_EXPECT(startInterims == 0);
	SIM_EXPECT(startTickets[0].Open());
	Motor::MotorReport report{};
	SIM_EXPECT(startTickets[0].Finish(Motor::MotorStatus::started, report) == ResultStatus::ok);
	Step(line, near, far, log, 1400);
	SIM_EXPECT(startFinals == 1);
	SIM_EXPECT(startResult == ResultStatus::ok);
}


// The link is down while the CANCEL is held and the request's deadline passes: the request ends with operationCancelled. The REQUEST was given
// to the stack while the link was up, and it is still in the control queue when the link goes down. The stack keeps no copy of a request its
// requester has closed: when the link returns, neither the REQUEST nor the held CANCEL is sent, so the device's handler does not run.
void TestHeldCancelDeadlineWhileLinkDownGivesOperationCancelled() {
	ResetCancelState();
	ReportingPacketLine line(VUB::Faults{});
	CancelRequester near(line.a);
	CancelResponder far(line.b);
	WireLog log;
	Establish(line, near, far, log);
	CancelHandle handle = near.Requests();
	uint16 number = SendStart(handle);
	SIM_EXPECT(number == 1);
	SIM_EXPECT(CancelStart(handle, number) == ResultStatus::ok);
	line.SetLinkUp(false);
	for (uint32 now = 1000; now <= 6500; now += 500) {
		Step(line, near, far, log, now);
	}

	SIM_EXPECT(startFinals == 1);
	SIM_EXPECT(startCancelledFinals == 1);
	SIM_EXPECT(startResult == ResultStatus::operationCancelled);
	SIM_EXPECT(startsSeen == 0);
	line.SetLinkUp(true);
	std::size_t cancelsOnWire = 0;
	for (uint32 now = 7000; now <= 9000; now += 500) {
		Step(line, near, far, log, now);
		cancelsOnWire += CountCancels(log.toFar, 1);
	}

	SIM_EXPECT(cancelsOnWire == 0);
	SIM_EXPECT(startsSeen == 0);
	SIM_EXPECT(cancelRuns == 0);
	SIM_EXPECT(startFinals == 1);
}


// A request given to the stack while the link was up, and still in its control queue when the link goes down, with its deadline passed
// while the link is down. When the link returns the request is not sent: the device's handler does not run, and the requester keeps its timeout.
void TestQueuedRequestOfClosedRequestIsNotSentWhenLinkReturns() {
	ResetCancelState();
	ReportingPacketLine line(VUB::Faults{});
	CancelRequester near(line.a);
	CancelResponder far(line.b);
	WireLog log;
	Establish(line, near, far, log);
	CancelHandle handle = near.Requests();
	SIM_EXPECT(SendStart(handle) == 1);
	line.SetLinkUp(false);
	for (uint32 now = 1000; now <= 6500; now += 500) {
		Step(line, near, far, log, now);
	}

	SIM_EXPECT(startFinals == 1);
	SIM_EXPECT(startResult == ResultStatus::timeout);
	line.SetLinkUp(true);
	for (uint32 now = 7000; now <= 9000; now += 500) {
		Step(line, near, far, log, now);
	}

	SIM_EXPECT(startsSeen == 0);
	SIM_EXPECT(startFinals == 1);
	SIM_EXPECT(startResult == ResultStatus::timeout);
}


// Without Reliable a request that waited in the control queue while the link was down goes out after its requester timed out (decision 16,
// as the wiki says). Its late final is dropped at the requester, which keeps its timeout.
void TestWithoutReliableQueuedRequestGoesOutAfterTimeout() {
	ResetCancelState();
	ReportingPacketLine line(VUB::Faults{});
	PlainCancelRequester near(line.a);
	PlainCancelResponder far(line.b);
	WireLog log;
	Step(line, near, far, log, 50);
	PlainCancelHandle handle = near.Requests();
	handle.SetSession(PlainSession);
	line.SetLinkUp(false);
	SIM_EXPECT(SendStart(handle) == 1);
	for (uint32 now = 1000; now <= 6500; now += 500) {
		Step(line, near, far, log, now);
	}

	SIM_EXPECT(startFinals == 1);
	SIM_EXPECT(startResult == ResultStatus::timeout);
	line.SetLinkUp(true);
	for (uint32 now = 7000; now <= 9000; now += 500) {
		Step(line, near, far, log, now);
	}

	SIM_EXPECT(startsSeen == 1);
	SIM_EXPECT(startFinals == 1);
	SIM_EXPECT(startResult == ResultStatus::timeout);
}


// Two requests given while the link is down go out in the order they were given when the link returns, each once.
void TestRequestsWaitingForLinkGoOutInOrder() {
	ResetCancelState();
	ReportingPacketLine line(VUB::Faults{});
	CancelRequester near(line.a);
	CancelResponder far(line.b);
	WireLog log;
	Establish(line, near, far, log);
	CancelHandle handle = near.Requests();
	line.SetLinkUp(false);
	Step(line, near, far, log, 1000);
	SIM_EXPECT(SendStart(handle) == 1);
	SIM_EXPECT(SendStart(handle) == 2);
	Step(line, near, far, log, 1100);
	line.SetLinkUp(true);
	Step(line, near, far, log, 1200);
	std::array<uint16, 2> numbers{};
	std::size_t starts = 0;
	std::size_t limit = log.toFar.count < log.toFar.packets.size() ? log.toFar.count : log.toFar.packets.size();
	for (std::size_t index = 0; index < limit; index++) {
		std::size_t length = log.toFar.lengths[index];
		if (length < StartRequestOne.size() || starts >= numbers.size()) {
			continue;
		}

		const std::array<uint8, 64>& packet = log.toFar.packets[index];
		std::size_t tailStart = length - StartRequestOneTail.size();
		bool ends = true;
		for (std::size_t byte = 0; byte < StartRequestOneTail.size() && ends; byte++) {
			ends = packet[tailStart + byte] == StartRequestOneTail[byte];
		}

		if (ends) {
			numbers[starts] = packet[length - 8];
			starts++;
		}
	}

	SIM_EXPECT(starts == 2);
	SIM_EXPECT(numbers[0] == 1);
	SIM_EXPECT(numbers[1] == 2);
	for (uint32 now = 1300; now <= 2000; now += 100) {
		Step(line, near, far, log, now);
	}

	SIM_EXPECT(startsSeen == 2);
	SIM_EXPECT(startFinals == 0);
}


// The REQUEST is lost on the line, and Reliable sends it again. The CANCEL is held all the while: it is not on the wire before the device has
// the REQUEST, and it goes out once the REQUEST is acknowledged. The device's cancel handler runs once.
void TestCancelHeldWhileRequestIsResent() {
	ResetCancelState();
	ReportingPacketLine line(VUB::Faults{});
	CancelRequester near(line.a);
	CancelResponder far(line.b);
	WireLog log;
	Establish(line, near, far, log);
	CancelHandle handle = near.Requests();
	LoseNext(lossToFar, StartRequestOneTail, 1);
	uint16 number = SendStart(handle);
	SIM_EXPECT(number == 1);
	SIM_EXPECT(CancelStart(handle, number) == ResultStatus::ok);
	std::size_t cancelsBeforeDevice = 0;
	std::size_t cancelsOnWire = 0;
	for (uint32 now = 1000; now <= 1800; now += 100) {
		Step(line, near, far, log, now);
		std::size_t cancels = CountCancels(log.toFar, 1);
		cancelsOnWire += cancels;
		if (startsSeen == 0) {
			cancelsBeforeDevice += cancels;
		}
	}

	SIM_EXPECT(cancelsBeforeDevice == 0);
	SIM_EXPECT(cancelsOnWire == 1);
	SIM_EXPECT(startsSeen == 1);
	SIM_EXPECT(cancelRuns == 1);
	SIM_EXPECT(startFinals == 1);
	SIM_EXPECT(startResult == ResultStatus::operationCancelled);
}


// Three requests are open and CancelAll cancels each of them: one CANCEL per number, and each device ticket runs the cancel handler once.
void TestCancelAllWithThreeRequests() {
	ResetCancelState();
	ReportingPacketLine line(VUB::Faults{});
	CancelRequester near(line.a);
	CancelResponderThree far(line.b);
	WireLog log;
	GiveSession(near);
	GiveSession(far);
	Step(line, near, far, log, 50);
	CancelHandle handle = near.Requests();
	Result<VUB::RequestId> probe = handle.Send<Command::stop, &OnStopFinal, &OnStopInterim>(VUB::NoBody{});
	SIM_EXPECT(probe.IsErr());
	for (uint32 round = 0; round < 4; round++) {
		Step(line, near, far, log, 900 + round);
	}

	SIM_EXPECT(SendStart(handle) == 1);
	SIM_EXPECT(SendStart(handle) == 2);
	SIM_EXPECT(SendStart(handle) == 3);
	Step(line, near, far, log, 1000);
	Step(line, near, far, log, 1100);
	SIM_EXPECT(startsSeen == 3);
	SIM_EXPECT(handle.CancelAll() == ResultStatus::ok);
	std::array<std::size_t, 4> cancelsPerNumber{};
	for (uint32 now = 1200; now <= 1500; now += 100) {
		Step(line, near, far, log, now);
		for (uint16 number = 1; number <= 3; number++) {
			cancelsPerNumber[number] += CountCancels(log.toFar, number);
		}
	}

	SIM_EXPECT(cancelsPerNumber[1] == 1);
	SIM_EXPECT(cancelsPerNumber[2] == 1);
	SIM_EXPECT(cancelsPerNumber[3] == 1);
	SIM_EXPECT(cancelRuns == 3);
	SIM_EXPECT(startFinals == 3);
	SIM_EXPECT(startCancelledFinals == 3);
}


// A request whose slot was freed by a cancel is not cancelled when its slot is taken again: the second request takes the first free slot,
// which the first request had, and a cancel of it sends its own CANCEL.
void TestSlotReusedAfterCancelIsNotCancelled() {
	ResetCancelState();
	ReportingPacketLine line(VUB::Faults{});
	CancelRequester near(line.a);
	CancelResponder far(line.b);
	WireLog log;
	Establish(line, near, far, log);
	CancelHandle handle = near.Requests();
	SIM_EXPECT(SendStart(handle) == 1);
	Step(line, near, far, log, 1000);
	Step(line, near, far, log, 1100);
	SIM_EXPECT(CancelStart(handle, 1) == ResultStatus::ok);
	for (uint32 now = 1200; now <= 1500; now += 100) {
		Step(line, near, far, log, now);
	}

	SIM_EXPECT(cancelRuns == 1);
	SIM_EXPECT(startFinals == 1);
	SIM_EXPECT(startResult == ResultStatus::operationCancelled);
	SIM_EXPECT(CancelStart(handle, 1) == ResultStatus::notFound);
	SIM_EXPECT(SendStart(handle) == 2);
	Step(line, near, far, log, 1600);
	Step(line, near, far, log, 1700);
	SIM_EXPECT(startsSeen == 2);
	std::size_t cancelsTwo = 0;
	SIM_EXPECT(CancelStart(handle, 2) == ResultStatus::ok);
	for (uint32 now = 1800; now <= 2100; now += 100) {
		Step(line, near, far, log, now);
		cancelsTwo += CountCancels(log.toFar, 2);
	}

	SIM_EXPECT(cancelsTwo == 1);
	SIM_EXPECT(cancelRuns == 2);
	SIM_EXPECT(startFinals == 2);
	SIM_EXPECT(startCancelledFinals == 2);
}


// The handle of the requester that the final handler below uses to cancel the second request. The test sets it and clears it.
inline CancelHandle* cancellingHandle = nullptr;
inline uint16 cancelledSecondNumber = 0;
inline std::size_t cancellingFinals = 0;


// The final of the first request. It cancels the second request from inside its handler.
void OnFinalThatCancelsSecond(const VUB::Final<Motor::MotorStatus, Motor::MotorReport>& reply) {
	(void)reply;
	cancellingFinals++;
	if (cancellingHandle != nullptr) {
		(void)cancellingHandle->Cancel<Command::start>(VUB::RequestId{cancelledSecondNumber});
	}
}


// The final of the first request cancels the second from its handler: the CANCEL goes out from inside the requester's Process, the device's
// cancel handler runs once for the second ticket, and the second request ends with operationCancelled.
void TestFinalCancelsAnotherRequestFromItsHandler() {
	ResetCancelState();
	ReportingPacketLine line(VUB::Faults{});
	CancelRequester near(line.a);
	CancelResponder far(line.b);
	WireLog log;
	Establish(line, near, far, log);
	CancelHandle handle = near.Requests();
	cancellingFinals = 0;
	cancellingHandle = &handle;
	Result<VUB::RequestId> first = handle.Send<Command::start, &OnFinalThatCancelsSecond, &OnStartInterim>(StartBody());
	SIM_EXPECT(first.IsOk());
	SIM_EXPECT(first.Value().number == 1);
	cancelledSecondNumber = SendStart(handle);
	SIM_EXPECT(cancelledSecondNumber == 2);
	Step(line, near, far, log, 1000);
	Step(line, near, far, log, 1100);
	SIM_EXPECT(startsSeen == 2);
	Motor::MotorReport report{};
	SIM_EXPECT(startTickets[0].Finish(Motor::MotorStatus::started, report) == ResultStatus::ok);
	for (uint32 now = 1200; now <= 1500; now += 100) {
		Step(line, near, far, log, now);
	}

	cancellingHandle = nullptr;
	SIM_EXPECT(cancellingFinals == 1);
	SIM_EXPECT(cancelRuns == 1);
	SIM_EXPECT(startFinals == 1);
	SIM_EXPECT(startCancelledFinals == 1);
	SIM_EXPECT(startTickets[1].Open() == false);
}


// The same requester and responder with an in-order Reliable under the layer. Its session is given to that layer.
using InOrderLayer = VUB::ReliableOf<40, 4, true>;


using InOrderCancelRequester = VUB::Stack<+[]() {
	return VUB::Builder()
		.Layer<VUB::Requests<CancelCommands, VUB::Handlers<>>>().Open(4)
		.Layer<InOrderLayer>().Retries(3).TimeoutMs(200).Window(4)
		.Link<ReportingPacketEnd>()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


using InOrderCancelResponder = VUB::Stack<+[]() {
	return VUB::Builder()
		.Layer<VUB::Requests<CancelCommands, CancelDeviceHandlers>>().Tickets(2)
		.Layer<InOrderLayer>().Retries(3).TimeoutMs(200).Window(4)
		.Link<ReportingPacketEnd>()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


using InOrderCancelHandle = decltype(std::declval<InOrderCancelRequester&>().Requests());


// The sessions of an in-order pair, as Establish gives the plain pair. The rounds end at 900 ms, as in Establish.
void EstablishInOrder(ReportingPacketLine& line, InOrderCancelRequester& near, InOrderCancelResponder& far, WireLog& log) {
	near.template Find<InOrderLayer>().SetSession(ReliableSession);
	far.template Find<InOrderLayer>().SetSession(ReliableSession);
	Step(line, near, far, log, 50);
	InOrderCancelHandle nearHandle = near.Requests();
	Result<VUB::RequestId> probe = nearHandle.Send<Command::stop, &OnStopFinal, &OnStopInterim>(VUB::NoBody{});
	SIM_EXPECT(probe.IsErr());
	std::array<uint8, 1> trigger = {0xEE};
	SIM_EXPECT(far.LayerOutbox<0>().Send(std::span<const uint8>(trigger.data(), trigger.size())) == ResultStatus::busy);
	for (uint32 round = 0; round < 3; round++) {
		Step(line, near, far, log, 900 + round);
	}
}


// The acknowledgement of the first data packet of a session ends with its number, 1, in little endian: the last two bytes of the packet. A
// loss rule can name it.
inline std::array<uint8, 2> AckOfFirstPacket = {0x01, 0x00};


// A request sent while the link is down, and its deadline passed while it is down: when the link returns the request is not sent, the device's
// handler does not run, and the next request goes out and runs once.
void TestRequestSentWhileDownIsNotRunAfterTimeout() {
	ResetCancelState();
	ReportingPacketLine line(VUB::Faults{});
	CancelRequester near(line.a);
	CancelResponder far(line.b);
	WireLog log;
	Establish(line, near, far, log);
	CancelHandle handle = near.Requests();
	line.SetLinkUp(false);
	Step(line, near, far, log, 1000);
	SIM_EXPECT(SendStart(handle) == 1);
	for (uint32 now = 1500; now <= 6500; now += 500) {
		Step(line, near, far, log, now);
	}

	SIM_EXPECT(startFinals == 1);
	SIM_EXPECT(startResult == ResultStatus::timeout);
	line.SetLinkUp(true);
	for (uint32 now = 7000; now <= 9000; now += 500) {
		Step(line, near, far, log, now);
	}

	SIM_EXPECT(startsSeen == 0);
	SIM_EXPECT(startFinals == 1);
	startAtOnce = true;
	SIM_EXPECT(SendStart(handle) == 2);
	for (uint32 now = 9500; now <= 10500; now += 500) {
		Step(line, near, far, log, now);
	}

	SIM_EXPECT(startsSeen == 1);
	SIM_EXPECT(startFinals == 2);
	SIM_EXPECT(startResult == ResultStatus::ok);
}


// The same with an in-order Reliable under the layer on both ends: the closed request's number is settled at the requester, and the next
// request's packet moves the receiver past it.
void TestInOrderRequestSentWhileDownIsNotRunAfterTimeout() {
	ResetCancelState();
	ReportingPacketLine line(VUB::Faults{});
	InOrderCancelRequester near(line.a);
	InOrderCancelResponder far(line.b);
	WireLog log;
	EstablishInOrder(line, near, far, log);
	auto handle = near.Requests();
	line.SetLinkUp(false);
	Step(line, near, far, log, 1000);
	SIM_EXPECT(SendStart(handle) == 1);
	for (uint32 now = 1500; now <= 6500; now += 500) {
		Step(line, near, far, log, now);
	}

	SIM_EXPECT(startFinals == 1);
	SIM_EXPECT(startResult == ResultStatus::timeout);
	line.SetLinkUp(true);
	for (uint32 now = 7000; now <= 9000; now += 500) {
		Step(line, near, far, log, now);
	}

	SIM_EXPECT(startsSeen == 0);
	startAtOnce = true;
	SIM_EXPECT(SendStart(handle) == 2);
	for (uint32 now = 9500; now <= 10500; now += 500) {
		Step(line, near, far, log, now);
	}

	SIM_EXPECT(startsSeen == 1);
	SIM_EXPECT(startFinals == 2);
	SIM_EXPECT(startResult == ResultStatus::ok);
}


// A request sent while the link is down and cancelled while it is down: the cancel is held with the REQUEST, the deadline ends the request with
// operationCancelled, and when the link returns neither packet goes out.
void TestRequestCancelledWhileDownIsNotRun() {
	ResetCancelState();
	ReportingPacketLine line(VUB::Faults{});
	CancelRequester near(line.a);
	CancelResponder far(line.b);
	WireLog log;
	Establish(line, near, far, log);
	CancelHandle handle = near.Requests();
	line.SetLinkUp(false);
	Step(line, near, far, log, 1000);
	uint16 number = SendStart(handle);
	SIM_EXPECT(number == 1);
	SIM_EXPECT(CancelStart(handle, number) == ResultStatus::ok);
	for (uint32 now = 1500; now <= 6500; now += 500) {
		Step(line, near, far, log, now);
	}

	SIM_EXPECT(startFinals == 1);
	SIM_EXPECT(startCancelledFinals == 1);
	line.SetLinkUp(true);
	std::size_t cancelsOnWire = 0;
	for (uint32 now = 7000; now <= 9000; now += 500) {
		Step(line, near, far, log, now);
		cancelsOnWire += CountCancels(log.toFar, 1);
	}

	SIM_EXPECT(cancelsOnWire == 0);
	SIM_EXPECT(startsSeen == 0);
	SIM_EXPECT(cancelRuns == 0);
	SIM_EXPECT(startFinals == 1);
}


// A request sent and cancelled while the link is down, and the link returns while the request is open: the REQUEST is delivered once, the held CANCEL
// follows its acknowledgement, and the device's cancel handler runs once.
void TestRequestCancelledWhileDownRunsCancelOnceWhenLinkReturns() {
	ResetCancelState();
	ReportingPacketLine line(VUB::Faults{});
	CancelRequester near(line.a);
	CancelResponder far(line.b);
	WireLog log;
	Establish(line, near, far, log);
	CancelHandle handle = near.Requests();
	line.SetLinkUp(false);
	Step(line, near, far, log, 1000);
	uint16 number = SendStart(handle);
	SIM_EXPECT(number == 1);
	SIM_EXPECT(CancelStart(handle, number) == ResultStatus::ok);
	for (uint32 now = 1500; now <= 2000; now += 500) {
		Step(line, near, far, log, now);
	}

	line.SetLinkUp(true);
	std::size_t cancelsOnWire = 0;
	for (uint32 now = 2500; now <= 4000; now += 100) {
		Step(line, near, far, log, now);
		cancelsOnWire += CountCancels(log.toFar, 1);
	}

	SIM_EXPECT(cancelsOnWire == 1);
	SIM_EXPECT(startsSeen == 1);
	SIM_EXPECT(cancelRuns == 1);
	SIM_EXPECT(startFinals == 1);
	SIM_EXPECT(startResult == ResultStatus::operationCancelled);
}


// A request sent while the link is down, and the link returns while the request is open: the request is delivered once and its final is ok.
void TestRequestSentWhileDownIsDeliveredOnceWhenLinkReturns() {
	ResetCancelState();
	startAtOnce = true;
	ReportingPacketLine line(VUB::Faults{});
	CancelRequester near(line.a);
	CancelResponder far(line.b);
	WireLog log;
	Establish(line, near, far, log);
	CancelHandle handle = near.Requests();
	line.SetLinkUp(false);
	Step(line, near, far, log, 1000);
	SIM_EXPECT(SendStart(handle) == 1);
	for (uint32 now = 1500; now <= 2000; now += 500) {
		Step(line, near, far, log, now);
	}

	line.SetLinkUp(true);
	for (uint32 now = 2500; now <= 3500; now += 500) {
		Step(line, near, far, log, now);
	}

	SIM_EXPECT(startsSeen == 1);
	SIM_EXPECT(startFinals == 1);
	SIM_EXPECT(startResult == ResultStatus::ok);
}


// A request given to the stack while the link was up, and the link flaps while the request is open: the device runs its handler once, and the
// request ends with its final.
void TestFlappingLinkDeliversOpenRequestOnce() {
	ResetCancelState();
	startAtOnce = true;
	ReportingPacketLine line(VUB::Faults{});
	CancelRequester near(line.a);
	CancelResponder far(line.b);
	WireLog log;
	Establish(line, near, far, log);
	CancelHandle handle = near.Requests();
	SIM_EXPECT(SendStart(handle) == 1);
	line.SetLinkUp(false);
	Step(line, near, far, log, 1000);
	line.SetLinkUp(true);
	Step(line, near, far, log, 1100);
	line.SetLinkUp(false);
	Step(line, near, far, log, 1200);
	line.SetLinkUp(true);
	Step(line, near, far, log, 1300);
	line.SetLinkUp(false);
	Step(line, near, far, log, 1400);
	line.SetLinkUp(true);
	for (uint32 now = 1500; now <= 3000; now += 100) {
		Step(line, near, far, log, now);
	}

	SIM_EXPECT(startsSeen == 1);
	SIM_EXPECT(startFinals == 1);
	SIM_EXPECT(startResult == ResultStatus::ok);
}


// A request sent while the link is down, whose deadline passes while the link is still down: the link then flaps, and the request is not sent
// on any of the flaps. The device's handler does not run.
void TestFlappingLinkDoesNotRunClosedRequest() {
	ResetCancelState();
	ReportingPacketLine line(VUB::Faults{});
	CancelRequester near(line.a);
	CancelResponder far(line.b);
	WireLog log;
	Establish(line, near, far, log);
	CancelHandle handle = near.Requests();
	line.SetLinkUp(false);
	Step(line, near, far, log, 1000);
	SIM_EXPECT(SendStart(handle) == 1);
	for (uint32 now = 1500; now <= 6500; now += 500) {
		Step(line, near, far, log, now);
	}

	SIM_EXPECT(startFinals == 1);
	SIM_EXPECT(startResult == ResultStatus::timeout);
	for (uint32 now = 6600; now <= 7400; now += 200) {
		line.SetLinkUp(((now / 200) % 2) == 0);
		Step(line, near, far, log, now);
	}

	line.SetLinkUp(true);
	for (uint32 now = 7500; now <= 8500; now += 500) {
		Step(line, near, far, log, now);
	}

	SIM_EXPECT(startsSeen == 0);
	SIM_EXPECT(startFinals == 1);
	SIM_EXPECT(startResult == ResultStatus::timeout);
}


// Two requests in flight over an outage: a stop, which closes at its deadline while the link is down, and a start, which is still open when the
// link returns. The start is delivered once and ends ok; the stop's handler does not run and its final is a timeout.
void TestClosedAndOpenRequestsInFlightOverOutage() {
	ResetCancelState();
	startAtOnce = true;
	ReportingPacketLine line(VUB::Faults{});
	CancelRequester near(line.a);
	CancelResponder far(line.b);
	WireLog log;
	Establish(line, near, far, log);
	CancelHandle handle = near.Requests();
	line.SetLinkUp(false);
	Step(line, near, far, log, 1000);
	SIM_EXPECT(SendStop(handle) == 1);
	SIM_EXPECT(SendStart(handle) == 2);
	for (uint32 now = 1500; now <= 3500; now += 500) {
		Step(line, near, far, log, now);
	}

	SIM_EXPECT(stopFinals == 1);
	SIM_EXPECT(stopResult == ResultStatus::timeout);
	line.SetLinkUp(true);
	for (uint32 now = 4000; now <= 5500; now += 250) {
		Step(line, near, far, log, now);
	}

	SIM_EXPECT(stopsSeen == 0);
	SIM_EXPECT(stopFinals == 1);
	SIM_EXPECT(startsSeen == 1);
	SIM_EXPECT(startFinals == 1);
	SIM_EXPECT(startResult == ResultStatus::ok);
}


// The REQUEST reaches the device and its acknowledgement is lost. The link goes down, and the deadline passes while it is down: the device's
// handler ran once, from the copy that arrived, and the request is not sent again when the link returns. The requester reports its timeout.
void TestAckLostThenDeadlineWhileDownRunsOnce() {
	ResetCancelState();
	ReportingPacketLine line(VUB::Faults{});
	CancelRequester near(line.a);
	CancelResponder far(line.b);
	WireLog log;
	Establish(line, near, far, log);
	CancelHandle handle = near.Requests();
	LoseNext(lossToNear, AckOfFirstPacket, 1);
	SIM_EXPECT(SendStart(handle) == 1);
	Step(line, near, far, log, 1000);
	SIM_EXPECT(startsSeen == 1);
	line.SetLinkUp(false);
	for (uint32 now = 1100; now <= 6500; now += 500) {
		Step(line, near, far, log, now);
	}

	SIM_EXPECT(startFinals == 1);
	SIM_EXPECT(startResult == ResultStatus::timeout);
	line.SetLinkUp(true);
	for (uint32 now = 7000; now <= 9000; now += 500) {
		Step(line, near, far, log, now);
	}

	SIM_EXPECT(startsSeen == 1);
	SIM_EXPECT(startFinals == 1);
	SIM_EXPECT(startResult == ResultStatus::timeout);
}


// The same loss, and the link returns while the request is open: the REQUEST is sent again, the device takes it as a duplicate and does not run its
// handler again, and the final that the ticket then gives reaches the requester as ok.
void TestAckLostThenLinkReturnsRunsOnce() {
	ResetCancelState();
	ReportingPacketLine line(VUB::Faults{});
	CancelRequester near(line.a);
	CancelResponder far(line.b);
	WireLog log;
	Establish(line, near, far, log);
	CancelHandle handle = near.Requests();
	LoseNext(lossToNear, AckOfFirstPacket, 1);
	SIM_EXPECT(SendStart(handle) == 1);
	Step(line, near, far, log, 1000);
	SIM_EXPECT(startsSeen == 1);
	line.SetLinkUp(false);
	for (uint32 now = 1100; now <= 1500; now += 100) {
		Step(line, near, far, log, now);
	}

	line.SetLinkUp(true);
	for (uint32 now = 1600; now <= 2500; now += 100) {
		Step(line, near, far, log, now);
	}

	SIM_EXPECT(startsSeen == 1);
	SIM_EXPECT(startFinals == 0);
	Motor::MotorReport report{};
	SIM_EXPECT(startTickets[0].Finish(Motor::MotorStatus::started, report) == ResultStatus::ok);
	for (uint32 now = 2600; now <= 3000; now += 100) {
		Step(line, near, far, log, now);
	}

	SIM_EXPECT(startsSeen == 1);
	SIM_EXPECT(startFinals == 1);
	SIM_EXPECT(startResult == ResultStatus::ok);
}


// The requester and the responder with no resend at all: Reliable gives up at the first timeout.
using ZeroRetriesRequester = VUB::Stack<+[]() {
	return VUB::Builder()
		.Layer<VUB::Requests<CancelCommands, VUB::Handlers<>>>().Open(4)
		.Layer<VUB::ReliableOf<40, 4>>().Retries(0).TimeoutMs(200).Window(4)
		.Link<ReportingPacketEnd>()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


using ZeroRetriesResponder = VUB::Stack<+[]() {
	return VUB::Builder()
		.Layer<VUB::Requests<CancelCommands, CancelDeviceHandlers>>().Tickets(2)
		.Layer<VUB::ReliableOf<40, 4>>().Retries(0).TimeoutMs(200).Window(4)
		.Link<ReportingPacketEnd>()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


// The requester and the responder over a reporting stream line: the requests layer, Reliable, and flag framing (a frame is 6 bytes longer
// than its packet, and a frame of a request is about 21 bytes).
using StreamCancelRequester = VUB::Stack<+[]() {
	return VUB::Builder()
		.Layer<VUB::Requests<CancelCommands, VUB::Handlers<>>>().Open(4)
		.Layer<VUB::ReliableOf<40, 4>>().Retries(3).TimeoutMs(200).Window(4)
		.Framing<FlagFraming>()
		.Link<ReportingStreamEnd>()
		.MaxPayload(40)
		.ReceiveRing(256)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


using StreamCancelResponder = VUB::Stack<+[]() {
	return VUB::Builder()
		.Layer<VUB::Requests<CancelCommands, CancelDeviceHandlers>>().Tickets(2)
		.Layer<VUB::ReliableOf<40, 4>>().Retries(3).TimeoutMs(200).Window(4)
		.Framing<FlagFraming>()
		.Link<ReportingStreamEnd>()
		.MaxPayload(40)
		.ReceiveRing(256)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


// With Retries(0) a request sent while the link is down is sent once, at the return, and its final arrives.
void TestRetriesZeroRequestSentWhileDownIsDelivered() {
	ResetCancelState();
	startAtOnce = true;
	ReportingPacketLine line(VUB::Faults{});
	ZeroRetriesRequester near(line.a);
	ZeroRetriesResponder far(line.b);
	WireLog log;
	Establish(line, near, far, log);
	auto handle = near.Requests();
	line.SetLinkUp(false);
	Step(line, near, far, log, 1000);
	SIM_EXPECT(SendStart(handle) == 1);
	for (uint32 now = 1100; now <= 1500; now += 100) {
		Step(line, near, far, log, now);
	}

	line.SetLinkUp(true);
	for (uint32 now = 1600; now <= 2500; now += 100) {
		Step(line, near, far, log, now);
	}

	SIM_EXPECT(startsSeen == 1);
	SIM_EXPECT(startFinals == 1);
	SIM_EXPECT(startResult == ResultStatus::ok);
}


// With Retries(0) a request whose packet left, with its acknowledgement lost, is sent again once when the link returns, and the device runs
// its handler once: the copy that arrived first, the second one is a duplicate.
void TestRetriesZeroAckLostThenLinkReturnsResendsOnce() {
	ResetCancelState();
	ReportingPacketLine line(VUB::Faults{});
	ZeroRetriesRequester near(line.a);
	ZeroRetriesResponder far(line.b);
	WireLog log;
	Establish(line, near, far, log);
	auto handle = near.Requests();
	LoseNext(lossToNear, AckOfFirstPacket, 1);
	SIM_EXPECT(SendStart(handle) == 1);
	Step(line, near, far, log, 1000);
	SIM_EXPECT(startsSeen == 1);
	line.SetLinkUp(false);
	for (uint32 now = 1100; now <= 1300; now += 100) {
		Step(line, near, far, log, now);
	}

	line.SetLinkUp(true);
	std::size_t copies = 0;
	for (uint32 now = 1400; now <= 2000; now += 100) {
		Step(line, near, far, log, now);
		copies += CountEndingWith(log.toFar, StartRequestOneTail);
	}

	SIM_EXPECT(copies == 1);
	SIM_EXPECT(startsSeen == 1);
	SIM_EXPECT(startFinals == 0);
	Motor::MotorReport report{};
	SIM_EXPECT(startTickets[0].Finish(Motor::MotorStatus::started, report) == ResultStatus::ok);
	for (uint32 now = 2100; now <= 2400; now += 100) {
		Step(line, near, far, log, now);
	}

	SIM_EXPECT(startsSeen == 1);
	SIM_EXPECT(startFinals == 1);
	SIM_EXPECT(startResult == ResultStatus::ok);
}


// With Retries(3) a request sent while the link is down has all its tries after the return: the line loses every copy, and the copies on the
// wire are four, then the request ends with retryExhausted.
void TestRequestSentWhileDownHasAllItsTriesAfterReturn() {
	ResetCancelState();
	ReportingPacketLine line(VUB::Faults{});
	CancelRequester near(line.a);
	CancelResponder far(line.b);
	WireLog log;
	Establish(line, near, far, log);
	auto handle = near.Requests();
	LoseNext(lossToFar, StartRequestOneTail, 100);
	line.SetLinkUp(false);
	Step(line, near, far, log, 1000);
	SIM_EXPECT(SendStart(handle) == 1);
	for (uint32 now = 1100; now <= 1500; now += 100) {
		Step(line, near, far, log, now);
	}

	line.SetLinkUp(true);
	std::size_t copies = 0;
	for (uint32 now = 1600; now <= 2600; now += 100) {
		Step(line, near, far, log, now);
		copies += CountEndingWith(log.toFar, StartRequestOneTail);
	}

	SIM_EXPECT(copies == 4);
	SIM_EXPECT(startFinals == 1);
	SIM_EXPECT(startResult == ResultStatus::retryExhausted);
}


// A request sent while the link is down, its frame half written at the fall, and its deadline passed while the link is down: when the link
// returns the frame is not written again, the device's handler does not run, and the next request is delivered once.
void TestStreamHalfWrittenRequestIsNotRunWhenClosed() {
	ResetCancelState();
	ReportingStreamLine line(VUB::Faults{});
	StreamCancelRequester near(line.a);
	StreamCancelResponder far(line.b);
	WireLog log;
	Establish(line, near, far, log);
	auto handle = near.Requests();
	SIM_EXPECT(SendStart(handle) == 1);
	StreamRound(line, near, far, 1000, 6, UnlimitedBytes);
	line.SetLinkUp(false);
	for (uint32 now = 1100; now <= 6500; now += 500) {
		StreamRound(line, near, far, now, UnlimitedBytes, UnlimitedBytes);
	}

	SIM_EXPECT(startFinals == 1);
	SIM_EXPECT(startResult == ResultStatus::timeout);
	line.SetLinkUp(true);
	for (uint32 now = 7000; now <= 9000; now += 500) {
		StreamRound(line, near, far, now, UnlimitedBytes, UnlimitedBytes);
	}

	SIM_EXPECT(startsSeen == 0);
	startAtOnce = true;
	SIM_EXPECT(SendStart(handle) == 2);
	for (uint32 now = 9500; now <= 10500; now += 500) {
		StreamRound(line, near, far, now, UnlimitedBytes, UnlimitedBytes);
	}

	SIM_EXPECT(startsSeen == 1);
	SIM_EXPECT(startFinals == 2);
	SIM_EXPECT(startResult == ResultStatus::ok);
}


// The same with the request still open when the link returns: the frame is written again once, from its first byte, and the device runs its
// handler once.
void TestStreamHalfWrittenRequestIsDeliveredOnceWhenOpen() {
	ResetCancelState();
	startAtOnce = true;
	ReportingStreamLine line(VUB::Faults{});
	StreamCancelRequester near(line.a);
	StreamCancelResponder far(line.b);
	WireLog log;
	Establish(line, near, far, log);
	auto handle = near.Requests();
	SIM_EXPECT(SendStart(handle) == 1);
	StreamRound(line, near, far, 1000, 6, UnlimitedBytes);
	line.SetLinkUp(false);
	for (uint32 now = 1100; now <= 1500; now += 100) {
		StreamRound(line, near, far, now, UnlimitedBytes, UnlimitedBytes);
	}

	line.SetLinkUp(true);
	for (uint32 now = 2000; now <= 3500; now += 500) {
		StreamRound(line, near, far, now, UnlimitedBytes, UnlimitedBytes);
	}

	SIM_EXPECT(startsSeen == 1);
	SIM_EXPECT(startFinals == 1);
	SIM_EXPECT(startResult == ResultStatus::ok);
}


// A packet that needs no acknowledgement (a CANCEL sent unsequenced, as a Fire is) is not marked: its frame half written at the fall is
// written again from its first byte when the link returns, and the device's cancel handler runs once.
void TestStreamUnacknowledgedFrameIsWrittenAgainFromItsStart() {
	ResetCancelState();
	ReportingStreamLine line(VUB::Faults{});
	StreamCancelRequester near(line.a);
	StreamCancelResponder far(line.b);
	WireLog log;
	Establish(line, near, far, log);
	auto handle = near.Requests();
	SIM_EXPECT(SendStart(handle) == 1);
	StreamRound(line, near, far, 1000, UnlimitedBytes, UnlimitedBytes);
	SIM_EXPECT(startsSeen == 1);
	VUB::Meta meta{};
	meta.flags = VUB::Meta::NoAcknowledgeFlag;
	SIM_EXPECT(near.LayerOutbox<0>().Send(std::span<const uint8>(CancelOne.data(), CancelOne.size()), meta) == ResultStatus::ok);
	StreamRound(line, near, far, 1100, 3, UnlimitedBytes);
	line.SetLinkUp(false);
	for (uint32 now = 1200; now <= 1400; now += 100) {
		StreamRound(line, near, far, now, UnlimitedBytes, UnlimitedBytes);
	}

	line.SetLinkUp(true);
	for (uint32 now = 1500; now <= 2500; now += 500) {
		StreamRound(line, near, far, now, UnlimitedBytes, UnlimitedBytes);
	}

	SIM_EXPECT(cancelRuns == 1);
}


// The calls of the Cancel section of Layer/Requests.md, as the section writes them. They are compiled here; the stack has no session, so
// nothing is sent.
template<typename HandleType>
void CancelWikiCalls(HandleType handle) {
	Result<VUB::RequestId> sent = handle.template Send<Command::start, &OnStartFinal, &OnStartInterim>(StartBody());
	if (sent.IsOk()) {
		ResultStatus cancelled = handle.template Cancel<Command::start>(sent.Value());
		(void)cancelled;
	}

	ResultStatus all = handle.CancelAll();
	(void)all;
}


void TestCancelWikiCallsCompile() {
	ResetCancelState();
	ReportingPacketLine line(VUB::Faults{});
	CancelRequester near(line.a);
	CancelHandle handle = near.Requests();
	CancelWikiCalls(handle);
	SIM_EXPECT(startsSeen == 0);
}


int main() {
	TestCancelWikiCallsCompile();
	TestCancelIsHeldUntilRequestIsAcknowledged();
	TestCancelGoesAtOnceWithoutReliable();
	TestRequestAndCancelInOneBurstRunNoHandler();
	TestCancelHandlerFinishesTicket();
	TestCancelLeftUnfinishedGetsOperationCancelled();
	TestFinalBeforeAcknowledgementDropsHeldCancel();
	TestResponderFinalFirstWinsOverCancel();
	TestCancelAfterFinalIsNotFound();
	TestCancelForUnknownNumberOrTagIsDropped();
	TestCancelOfNotCancellableCommandGetsRefusedInterim();
	TestSecondCancelChangesNothing();
	TestCancelAllCancelsEveryOpenStart();
	TestCancelOfUnknownIdAndCancelAllWithNothingOpenSendNothing();
	TestDeadlineWhileCancellingGivesOperationCancelled();
	TestLostCancelIsResentByReliable();
	TestLostCancelWithoutReliableEndsAtDeadline();
	TestCancelHeldWhileLinkIsDown();
	TestTicketDeadlineRunsCancelHandlerOnce();
	TestPeerRestartRunsCancelHandlerOnceAndClosesTicket();
	TestDefaultTicketIsClosed();
	TestRequestSentAfterCancelAllIsNotCancelled();
	TestCancelAllLeavesNotCancellableRequestsOpen();
	TestCancelOfNumberZeroIsDropped();
	TestHeldCancelDeadlineWhileLinkDownGivesOperationCancelled();
	TestQueuedRequestOfClosedRequestIsNotSentWhenLinkReturns();
	TestRequestSentWhileDownIsNotRunAfterTimeout();
	TestInOrderRequestSentWhileDownIsNotRunAfterTimeout();
	TestRequestCancelledWhileDownIsNotRun();
	TestRequestCancelledWhileDownRunsCancelOnceWhenLinkReturns();
	TestRequestSentWhileDownIsDeliveredOnceWhenLinkReturns();
	TestFlappingLinkDeliversOpenRequestOnce();
	TestFlappingLinkDoesNotRunClosedRequest();
	TestClosedAndOpenRequestsInFlightOverOutage();
	TestAckLostThenDeadlineWhileDownRunsOnce();
	TestAckLostThenLinkReturnsRunsOnce();
	TestRequestsWaitingForLinkGoOutInOrder();
	TestRetriesZeroRequestSentWhileDownIsDelivered();
	TestRetriesZeroAckLostThenLinkReturnsResendsOnce();
	TestRequestSentWhileDownHasAllItsTriesAfterReturn();
	TestStreamHalfWrittenRequestIsNotRunWhenClosed();
	TestStreamHalfWrittenRequestIsDeliveredOnceWhenOpen();
	TestStreamUnacknowledgedFrameIsWrittenAgainFromItsStart();
	TestWithoutReliableQueuedRequestGoesOutAfterTimeout();
	TestCancelHeldWhileRequestIsResent();
	TestCancelAllWithThreeRequests();
	TestSlotReusedAfterCancelIsNotCancelled();
	TestFinalCancelsAnotherRequestFromItsHandler();
	return SimFinish("TestVubRequestsCancel");
}
