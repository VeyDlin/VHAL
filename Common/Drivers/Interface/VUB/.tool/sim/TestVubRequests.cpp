#include "Chains.h"
#include "ReliableChains.h"
#include "RequestsMotor.h"
#include "SimCheck.h"
#include <Drivers/Interface/VUB/Outbox.h>
#include <Drivers/Interface/VUB/Requests.h>
#include <Utilities/DataTypes.h>
#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <utility>

// The requests layer over a Reliable link, two stacks on a reporting packet line: a requester (near, end a) and a responder (far, end b).
// Each test starts from zero. The clock is testNowUs of Chains.h, shared by both stacks.


// The requester's commands: the device's start and stop with their ids, and an id the device does not serve. The stop fails while the link
// is down.
using RequesterCommands = VUB::CommandSet<+[]() {
	return VUB::Commands()
		.Command<"motor.start", 0x12>()
			.Request<Motor::MotorStart>()
			.Reply<Motor::MotorStatus, Motor::MotorReport>()
			.Interim()
			.ReplyTimeoutMs(5000)
		.Command<"motor.stop", 0x13>()
			.Reply<Motor::MotorStatus>()
			.ReplyTimeoutMs(2000)
			.FailWhileDown()
		.Command<"motor.unknown", 0x77>()
			.Reply<Motor::MotorStatus>()
			.ReplyTimeoutMs(2000);
}>;


using StartTicket = VUB::Ticket<Motor::MotorCommands, "motor.start">;
using StopTicket = VUB::Ticket<Motor::MotorCommands, "motor.stop">;


inline bool startInterimAtOnce = false;
inline bool startAtOnce = false;
inline bool stopAtOnce = false;
inline std::size_t startsSeen = 0;
inline std::size_t stopsSeen = 0;
inline StartTicket heldStart;
inline StopTicket heldStop;
inline bool startHeld = false;
inline bool stopHeld = false;


void OnStartDevice(StartTicket ticket, const Motor::MotorStart& body) {
	(void)body;
	startsSeen++;
	heldStart = ticket;
	startHeld = true;
	if (startInterimAtOnce) {
		(void)ticket.Interim(Motor::MotorStatus::warming);
	}

	if (startAtOnce) {
		Motor::MotorReport report{};
		report.speedRpm = 1200;
		report.currentMa = 850;
		(void)ticket.Finish(Motor::MotorStatus::started, report);
	}
}


void OnStopDevice(StopTicket ticket) {
	stopsSeen++;
	heldStop = ticket;
	stopHeld = true;
	if (stopAtOnce) {
		(void)ticket.Finish(Motor::MotorStatus::started);
	}
}


// The device's handlers: each request is held by its ticket unless the test says it is answered at once.
using DeviceHandlers = VUB::Handlers<
	VUB::On<"motor.start", &OnStartDevice>,
	VUB::On<"motor.stop", &OnStopDevice>>;


// The requester's results, recorded by each handler.
inline std::size_t startFinals = 0;
inline ResultStatus startResult = ResultStatus::ok;
inline Motor::MotorStatus startStatus = Motor::MotorStatus::started;
inline Motor::MotorReport startReport{};
inline std::size_t startInterims = 0;
inline Motor::MotorStatus lastInterimStatus = Motor::MotorStatus::started;
inline std::size_t stopFinals = 0;
inline ResultStatus stopResult = ResultStatus::ok;
inline std::size_t unknownFinals = 0;
inline ResultStatus unknownResult = ResultStatus::ok;


void OnStartFinalR(const VUB::Final<Motor::MotorStatus, Motor::MotorReport>& reply) {
	startFinals++;
	startResult = reply.result;
	startStatus = reply.status;
	startReport = reply.body;
}


void OnStartInterimR(const VUB::Progress<Motor::MotorStatus>& progress) {
	startInterims++;
	lastInterimStatus = progress.status;
}


void OnStopFinalR(const VUB::Final<Motor::MotorStatus, VUB::NoBody>& reply) {
	stopFinals++;
	stopResult = reply.result;
}


void OnUnknownFinalR(const VUB::Final<Motor::MotorStatus, VUB::NoBody>& reply) {
	unknownFinals++;
	unknownResult = reply.result;
}


// The requester: four slots, the Reliable layer under it, and the reporting line's ends.
using Requester = VUB::Stack<+[]() {
	return VUB::Builder()
		.Layer<VUB::Requests<RequesterCommands, VUB::Handlers<>>>().Open(4)
		.Layer<VUB::ReliableOf<40, 4>>().Retries(3).TimeoutMs(200).Window(4)
		.Link<ReportingPacketEnd>()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


// The responder: two tickets, the same Reliable layer.
using Responder = VUB::Stack<+[]() {
	return VUB::Builder()
		.Layer<VUB::Requests<Motor::MotorCommands, DeviceHandlers>>().Tickets(2)
		.Layer<VUB::ReliableOf<40, 4>>().Retries(3).TimeoutMs(200).Window(4)
		.Link<ReportingPacketEnd>()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


// Sends the packets of one direction to the stack on the far end, and keeps a copy of each.
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


// The packets of one round, by direction: toFar carries the requester's packets, toNear the responder's.
struct WireLog {
	ReliableCapture toFar;
	ReliableCapture toNear;


	void Clear() {
		toFar = ReliableCapture{};
		toNear = ReliableCapture{};
	}
};


// One round: the requester runs, its packets reach the responder, the responder runs, its packets reach the requester, the requester runs.
void Run(ReportingPacketLine& line, Requester& near, Responder& far, WireLog& log, uint32 nowUs) {
	testNowUs = nowUs;
	log.Clear();
	near.Process();
	Tee<Responder> forward{&log.toFar, &far};
	line.DeliverToB(forward);
	far.Process();
	Tee<Requester> back{&log.toNear, &near};
	line.DeliverToA(back);
	near.Process();
}


// True when the packet index of the capture ends with the expected bytes (the layer's payload follows Reliable's header).
template<std::size_t Size>
bool EndsWith(const ReliableCapture& capture, std::size_t index, const std::array<uint8, Size>& tail) {
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


// The sessions of both ends. Each end starts its sending session with one send that is busy until the sync is acknowledged: the requester's
// through its requests handle, the responder's through the handle of its requests layer (a reply goes out only in an open session).
void Establish(ReportingPacketLine& line, Requester& near, Responder& far, WireLog& log) {
	GiveSession(near);
	GiveSession(far);
	// The stack samples the link in its first Process. A FailWhileDown send before that is refused with noConnection, so the round comes
	// first: the session then starts with the first send the layer below takes.
	Run(line, near, far, log, 50);
	auto handle = near.Requests();
	auto probe = handle.Send<"motor.stop", &OnStopFinalR>(VUB::NoBody{});
	SIM_EXPECT(probe.IsErr());
	SIM_EXPECT(probe.Error() == ResultStatus::busy);
	std::array<uint8, 1> trigger = {0xEE};
	SIM_EXPECT(far.LayerOutbox<0>().Send(std::span<const uint8>(trigger.data(), trigger.size())) == ResultStatus::busy);
	for (uint32 round = 0; round < 3; round++) {
		Run(line, near, far, log, 100 + round);
	}
}


// A motor.stop sent to the device, with its final recorded: true when the request was accepted. The helpers keep the template arguments out of
// the check macro.
template<typename HandleType>
bool SendStopR(HandleType& handle) {
	auto sent = handle.template Send<"motor.stop", &OnStopFinalR>(VUB::NoBody{});
	return sent.IsErr() == false;
}


template<typename HandleType>
bool SendStartR(HandleType& handle) {
	auto sent = handle.template Send<"motor.start", &OnStartFinalR, &OnStartInterimR>(Motor::MotorStart{});
	return sent.IsErr() == false;
}


void ResetRequestsState() {
	ResetReliableState();
	startInterimAtOnce = false;
	startAtOnce = false;
	stopAtOnce = false;
	startsSeen = 0;
	stopsSeen = 0;
	startHeld = false;
	stopHeld = false;
	startFinals = 0;
	startResult = ResultStatus::ok;
	startInterims = 0;
	stopFinals = 0;
	stopResult = ResultStatus::ok;
	unknownFinals = 0;
	unknownResult = ResultStatus::ok;
}


// A request answered at once: one packet goes back, and it carries the final, with the acknowledgement of the request.
void TestReplyAtOnce() {
	ResetRequestsState();
	ReportingPacketLine line(VUB::Faults{});
	Requester near(line.a);
	Responder far(line.b);
	WireLog log;
	Establish(line, near, far, log);
	stopAtOnce = true;

	auto handle = near.Requests();
	auto sent = handle.Send<"motor.stop", &OnStopFinalR>(VUB::NoBody{});
	SIM_EXPECT(sent.IsErr() == false);
	SIM_EXPECT(sent.Value().number == 1);
	Run(line, near, far, log, 1000);
	SIM_EXPECT(stopsSeen == 1);
	SIM_EXPECT(stopFinals == 1);
	SIM_EXPECT(stopResult == ResultStatus::ok);
	// The request is a Reliable packet whose payload ends with motor.stop, number 1, tag 1; the reply is one packet ending with the final.
	std::array<uint8, 5> request = {0x01, 0x13, 0x01, 0x00, 0x01};
	std::array<uint8, 6> finalStarted = {0x03, 0x01, 0x00, 0x01, 0x00, 0x00};
	SIM_EXPECT(log.toFar.count == 1);
	SIM_EXPECT(EndsWith(log.toFar, 0, request));
	SIM_EXPECT(log.toNear.count == 1);
	SIM_EXPECT(EndsWith(log.toNear, 0, finalStarted));
	// The one packet of the reply carries the acknowledgement of the request: its flags have the acknowledgement bit, and it is not a bare ack.
	SIM_EXPECT((log.toNear.packets[0][0] & ReliableLayer::AcknowledgesFlag) != 0);
	SIM_EXPECT((log.toNear.packets[0][0] & ReliableLayer::ControlFlag) == 0);
}


// A request answered later: nothing is final until the ticket finishes; then the final goes out in the next round.
void TestReplyLater() {
	ResetRequestsState();
	ReportingPacketLine line(VUB::Faults{});
	Requester near(line.a);
	Responder far(line.b);
	WireLog log;
	Establish(line, near, far, log);

	auto handle = near.Requests();
	SIM_EXPECT(SendStopR(handle));
	Run(line, near, far, log, 1000);
	Run(line, near, far, log, 2000);
	SIM_EXPECT(stopHeld);
	SIM_EXPECT(stopFinals == 0);
	SIM_EXPECT(heldStop.Open());
	SIM_EXPECT(heldStop.Finish(Motor::MotorStatus::started) == ResultStatus::ok);
	Run(line, near, far, log, 3000);
	SIM_EXPECT(stopFinals == 1);
	SIM_EXPECT(stopResult == ResultStatus::ok);
	SIM_EXPECT(heldStop.Open() == false);
}


// Interim replies arrive in order, before the final; the last one is the device's own.
void TestInterimReplies() {
	ResetRequestsState();
	ReportingPacketLine line(VUB::Faults{});
	Requester near(line.a);
	Responder far(line.b);
	WireLog log;
	Establish(line, near, far, log);
	startInterimAtOnce = true;

	auto handle = near.Requests();
	SIM_EXPECT(SendStartR(handle));
	Run(line, near, far, log, 1000);
	SIM_EXPECT(startInterims == 1);
	SIM_EXPECT(lastInterimStatus == Motor::MotorStatus::warming);
	SIM_EXPECT(startFinals == 0);

	SIM_EXPECT(startHeld);
	SIM_EXPECT(heldStart.Interim(Motor::MotorStatus::started) == ResultStatus::ok);
	Run(line, near, far, log, 2000);
	SIM_EXPECT(startInterims == 2);
	SIM_EXPECT(lastInterimStatus == Motor::MotorStatus::started);

	Motor::MotorReport report{};
	report.speedRpm = 1200;
	report.currentMa = 850;
	SIM_EXPECT(heldStart.Finish(Motor::MotorStatus::started, report) == ResultStatus::ok);
	Run(line, near, far, log, 3000);
	SIM_EXPECT(startFinals == 1);
	SIM_EXPECT(startResult == ResultStatus::ok);
	SIM_EXPECT(startStatus == Motor::MotorStatus::started);
	SIM_EXPECT(startReport.speedRpm == 1200);
	SIM_EXPECT(startReport.currentMa == 850);
	SIM_EXPECT(startInterims == 2);
}


// The requester's deadline: the device holds the request and never answers, so the request ends with timeout. A late final is dropped.
void TestRequesterTimeoutAndLateFinal() {
	ResetRequestsState();
	ReportingPacketLine line(VUB::Faults{});
	Requester near(line.a);
	Responder far(line.b);
	WireLog log;
	Establish(line, near, far, log);

	auto handle = near.Requests();
	SIM_EXPECT(SendStopR(handle));
	uint32 now = 1000;
	for (int step = 0; step < 25; step++) {
		Run(line, near, far, log, now);
		now += 100000;
	}

	SIM_EXPECT(stopFinals == 1);
	SIM_EXPECT(stopResult == ResultStatus::timeout);
	// The device's ticket is closed by its own deadline (or by the requester's final timeout): a late finish is notAvailable, and no second
	// final reaches the requester.
	SIM_EXPECT(heldStop.Open() == false);
	SIM_EXPECT(heldStop.Finish(Motor::MotorStatus::started) == ResultStatus::notAvailable);
	Run(line, near, far, log, now);
	SIM_EXPECT(stopFinals == 1);
}


// Retries end a request with retryExhausted: the wire is cut after the session starts, so no acknowledgement comes back.
void TestRetryExhausted() {
	ResetRequestsState();
	ReportingPacketLine line(VUB::Faults{});
	Requester near(line.a);
	Responder far(line.b);
	WireLog log;
	Establish(line, near, far, log);

	auto handle = near.Requests();
	SIM_EXPECT(SendStopR(handle));
	uint32 now = 1000;
	for (int step = 0; step < 12; step++) {
		testNowUs = now;
		near.Process();
		now += 100000;
	}

	SIM_EXPECT(stopFinals == 1);
	SIM_EXPECT(stopResult == ResultStatus::retryExhausted);
}


// The device's ticket has its own deadline: when the request is never finished, the ticket is closed and its final is a timeout.
void TestTicketTimeout() {
	ResetRequestsState();
	ReportingPacketLine line(VUB::Faults{});
	Requester near(line.a);
	Responder far(line.b);
	WireLog log;
	Establish(line, near, far, log);

	auto handle = near.Requests();
	SIM_EXPECT(SendStopR(handle));
	Run(line, near, far, log, 1000);
	SIM_EXPECT(stopHeld);
	SIM_EXPECT(heldStop.Open());
	uint32 now = 1000;
	for (int step = 0; step < 25; step++) {
		now += 100000;
		Run(line, near, far, log, now);
	}

	SIM_EXPECT(heldStop.Open() == false);
	SIM_EXPECT(heldStop.Finish(Motor::MotorStatus::started) == ResultStatus::notAvailable);
}


// The responder restarts while a request is open: the requester closes every open request of the peer with reset.
void TestPeerRestartClosesOpenRequests() {
	ResetRequestsState();
	ReportingPacketLine line(VUB::Faults{});
	Requester near(line.a);
	std::optional<Responder> far;
	far.emplace(line.b);
	WireLog log;
	Establish(line, near, *far, log);

	auto handle = near.Requests();
	SIM_EXPECT(SendStopR(handle));
	Run(line, near, *far, log, 1000);
	SIM_EXPECT(stopHeld);
	SIM_EXPECT(stopFinals == 0);

	far.reset();
	far.emplace(line.b);
	far->Process();
	// The requester has nothing in flight after the acknowledgement, so the new boot shows only when it sends again: a second request is
	// that traffic. The restart closes both open requests of the peer with reset.
	SIM_EXPECT(SendStopR(handle));
	uint32 now = 2000;
	for (int step = 0; step < 40 && stopFinals < 2; step++) {
		Run(line, near, *far, log, now);
		now += 100000;
	}

	SIM_EXPECT(stopFinals == 2);
	SIM_EXPECT(stopResult == ResultStatus::reset);
}


// The link goes down: a FailWhileDown request is closed with noConnection, and a new one of it is refused at once; a request of a command
// that waits is kept, and its deadline ends it.
void TestLinkDown() {
	ResetRequestsState();
	ReportingPacketLine line(VUB::Faults{});
	Requester near(line.a);
	Responder far(line.b);
	WireLog log;
	Establish(line, near, far, log);

	auto handle = near.Requests();
	SIM_EXPECT(SendStopR(handle));
	Run(line, near, far, log, 1000);
	SIM_EXPECT(stopHeld);

	line.SetLinkUp(false);
	testNowUs = 2000;
	near.Process();
	far.Process();
	near.Process();
	SIM_EXPECT(stopFinals == 1);
	SIM_EXPECT(stopResult == ResultStatus::noConnection);
	auto refused = handle.Send<"motor.stop", &OnStopFinalR>(VUB::NoBody{});
	SIM_EXPECT(refused.IsErr());
	SIM_EXPECT(refused.Error() == ResultStatus::noConnection);

	// motor.start waits: the send is accepted while down, and its deadline ends it.
	auto waiting = handle.Send<"motor.start", &OnStartFinalR>(Motor::MotorStart{});
	SIM_EXPECT(waiting.IsErr() == false);
	uint32 now = 3000;
	for (int step = 0; step < 60 && startFinals == 0; step++) {
		testNowUs = now;
		near.Process();
		now += 100000;
	}

	SIM_EXPECT(startFinals == 1);
	SIM_EXPECT(startResult == ResultStatus::timeout);
	line.SetLinkUp(true);
}


// Refusals of the responder: the third stop finds both tickets taken (busy); an id the device does not serve is notSupported.
void TestRefusals() {
	ResetRequestsState();
	ReportingPacketLine line(VUB::Faults{});
	Requester near(line.a);
	Responder far(line.b);
	WireLog log;
	Establish(line, near, far, log);

	auto handle = near.Requests();
	SIM_EXPECT(SendStopR(handle));
	SIM_EXPECT(SendStopR(handle));
	Run(line, near, far, log, 1000);
	SIM_EXPECT(stopsSeen == 2);
	SIM_EXPECT(stopFinals == 0);
	SIM_EXPECT(SendStopR(handle));
	Run(line, near, far, log, 2000);
	SIM_EXPECT(stopFinals == 1);
	SIM_EXPECT(stopResult == ResultStatus::busy);

	auto unknown = handle.Send<"motor.unknown", &OnUnknownFinalR>(VUB::NoBody{});
	SIM_EXPECT(unknown.IsErr() == false);
	Run(line, near, far, log, 3000);
	SIM_EXPECT(unknownFinals == 1);
	SIM_EXPECT(unknownResult == ResultStatus::notSupported);
}


// Interim and final of a request the device finishes at once, with the start's report: the requester sees the status and the body.
void TestFinalWithReport() {
	ResetRequestsState();
	ReportingPacketLine line(VUB::Faults{});
	Requester near(line.a);
	Responder far(line.b);
	WireLog log;
	Establish(line, near, far, log);
	startAtOnce = true;

	auto handle = near.Requests();
	SIM_EXPECT(SendStartR(handle));
	Run(line, near, far, log, 1000);
	SIM_EXPECT(startFinals == 1);
	SIM_EXPECT(startReport.speedRpm == 1200);
	SIM_EXPECT(startReport.currentMa == 850);
	SIM_EXPECT(startInterims == 0);
}


// The full table, the number wrap with a late final, and a busy send that uses no number. These need no stack: a requester layer with
// a recording outbox, and the packets it sends and takes are hand-written.
using TableLayer = VUB::RequestsOf<RequesterCommands, VUB::Handlers<>, VUB::RequestsOptions{2, 0, 0}>;


struct Probe {
	std::array<uint8, 16> last{};
	std::size_t length = 0;
	std::size_t count = 0;
	ResultStatus answer = ResultStatus::ok;
};


ResultStatus ProbeSend(void* owner, std::span<const uint8> packet, const VUB::Meta&) {
	Probe* probe = static_cast<Probe*>(owner);
	if (probe->answer != ResultStatus::ok) {
		return probe->answer;
	}

	for (std::size_t index = 0; index < packet.size() && index < probe->last.size(); index++) {
		probe->last[index] = packet[index];
	}

	probe->length = packet.size();
	probe->count++;
	return ResultStatus::ok;
}


VUB::Outbox ProbeOutbox(Probe& probe) {
	return VUB::Outbox(&probe, &ProbeSend, nullptr, nullptr);
}


// A FINAL of a number and tag, with the code 0 and status 0, for the table's requests.
std::array<uint8, 6> FinalOk(uint16 number, uint8 tag) {
	return {0x03, static_cast<uint8>(number & 0xFF), static_cast<uint8>(number >> 8), tag, 0x00, 0x00};
}


inline std::size_t tableFinals = 0;
inline ResultStatus tableResult = ResultStatus::notAvailable;


void OnTableFinal(const VUB::Final<Motor::MotorStatus, VUB::NoBody>& reply) {
	tableFinals++;
	tableResult = reply.result;
}


// Two slots: the third send is busy, and it uses no number, so the send after the first final takes number 3.
void TestFullTableAndBusySend() {
	tableFinals = 0;
	TableLayer layer;
	Probe probe;
	TableLayer::Handle handle(layer, ProbeOutbox(probe));
	auto first = handle.Send<"motor.stop", &OnTableFinal>(VUB::NoBody{});
	auto second = handle.Send<"motor.stop", &OnTableFinal>(VUB::NoBody{});
	SIM_EXPECT(first.IsErr() == false);
	SIM_EXPECT(second.IsErr() == false);
	SIM_EXPECT(first.Value().number == 1);
	SIM_EXPECT(second.Value().number == 2);
	auto third = handle.Send<"motor.stop", &OnTableFinal>(VUB::NoBody{});
	SIM_EXPECT(third.IsErr());
	SIM_EXPECT(third.Error() == ResultStatus::busy);

	std::array<uint8, 6> finalOne = FinalOk(1, 1);
	SIM_EXPECT(layer.Take(VUB::Meta(), std::span<const uint8>(finalOne.data(), finalOne.size())).IsErr() == false);
	layer.Process(1000, ProbeOutbox(probe));
	SIM_EXPECT(tableFinals == 1);
	SIM_EXPECT(tableResult == ResultStatus::ok);

	// The stack refuses the next send once: no number is used, and the send after it gets number 3.
	probe.answer = ResultStatus::busy;
	auto refused = handle.Send<"motor.stop", &OnTableFinal>(VUB::NoBody{});
	SIM_EXPECT(refused.IsErr());
	SIM_EXPECT(refused.Error() == ResultStatus::busy);
	probe.answer = ResultStatus::ok;
	auto third2 = handle.Send<"motor.stop", &OnTableFinal>(VUB::NoBody{});
	SIM_EXPECT(third2.IsErr() == false);
	SIM_EXPECT(third2.Value().number == 3);
	SIM_EXPECT(probe.length == 5);
	std::array<uint8, 5> stopThree = {0x01, 0x13, 0x03, 0x00, 0x03};
	for (std::size_t index = 0; index < stopThree.size(); index++) {
		SIM_EXPECT(probe.last[index] == stopThree[index]);
	}
}


// The number wraps after 65535 requests: the next one is number 1 with the tag the counter gives (0), not the tag of the old number 1
// (1). A late final of the old request, with its old tag, does not close the new one; the new one's final does.
void TestNumberWrapWithLateFinal() {
	tableFinals = 0;
	TableLayer layer;
	Probe probe;
	TableLayer::Handle handle(layer, ProbeOutbox(probe));
	for (uint32 count = 0; count < 65535; count++) {
		auto sent = handle.Send<"motor.stop", &OnTableFinal>(VUB::NoBody{});
		SIM_EXPECT(sent.IsErr() == false);
		uint16 number = sent.Value().number;
		uint8 tag = static_cast<uint8>(probe.last[4]);
		std::array<uint8, 6> answer = FinalOk(number, tag);
		(void)layer.Take(VUB::Meta(), std::span<const uint8>(answer.data(), 6));
		layer.Process(1000, ProbeOutbox(probe));
	}

	SIM_EXPECT(tableFinals == 65535);
	auto wrapped = handle.Send<"motor.stop", &OnTableFinal>(VUB::NoBody{});
	SIM_EXPECT(wrapped.IsErr() == false);
	SIM_EXPECT(wrapped.Value().number == 1);
	std::array<uint8, 5> stopOne = {0x01, 0x13, 0x01, 0x00, 0x00};
	for (std::size_t index = 0; index < stopOne.size(); index++) {
		SIM_EXPECT(probe.last[index] == stopOne[index]);
	}

	std::array<uint8, 6> lateOld = {0x03, 0x01, 0x00, 0x01, 0x00, 0x00};
	SIM_EXPECT(layer.Take(VUB::Meta(), std::span<const uint8>(lateOld.data(), lateOld.size())).IsErr() == false);
	layer.Process(2000, ProbeOutbox(probe));
	SIM_EXPECT(tableFinals == 65535);

	std::array<uint8, 6> newOne = {0x03, 0x01, 0x00, 0x00, 0x00, 0x00};
	SIM_EXPECT(layer.Take(VUB::Meta(), std::span<const uint8>(newOne.data(), newOne.size())).IsErr() == false);
	layer.Process(3000, ProbeOutbox(probe));
	SIM_EXPECT(tableFinals == 65536);
	SIM_EXPECT(tableResult == ResultStatus::ok);
}


// True when the last packet a probe recorded is exactly the expected bytes.
template<std::size_t Size>
bool ProbeLastIs(const Probe& probe, const std::array<uint8, Size>& expected) {
	if (probe.length != Size) {
		return false;
	}

	for (std::size_t index = 0; index < Size; index++) {
		if (probe.last[index] != expected[index]) {
			return false;
		}
	}

	return true;
}


// The packets of a line that are lost: a sink that takes what the line delivers and passes none of it on.
struct LostSink {
	void Feed(uint8) {
	}


	void FeedPacket(std::span<const uint8>) {
	}
};


// One round of a stack pair, in the order Run uses. The stacks may differ in their layers, so the round is a template.
template<typename NearStack, typename FarStack>
void RoundOf(ReportingPacketLine& line, NearStack& near, FarStack& far, WireLog& log, uint32 nowUs) {
	testNowUs = nowUs;
	log.Clear();
	near.Process();
	Tee<FarStack> forward{&log.toFar, &far};
	line.DeliverToB(forward);
	far.Process();
	Tee<NearStack> back{&log.toNear, &near};
	line.DeliverToA(back);
	near.Process();
}


// The responder's commands for the no-reply and the fire: a stop with a ticket, a reset with no reply, and a kick with a body and no reply.
// The order in which the handlers run is recorded, so a test can check it.
using ServeCommands = VUB::CommandSet<+[]() {
	return VUB::Commands()
		.Command<"motor.stop", 0x13>()
			.Reply<Motor::MotorStatus>()
			.ReplyTimeoutMs(2000)
			.TicketTimeoutMs(60000)
		.Command<"motor.reset", 0x14>()
			.NoReply()
		.Command<"motor.kick", 0x15>()
			.Request<Motor::MotorFault>()
			.Fire();
}>;


using ServeStopTicket = VUB::Ticket<ServeCommands, "motor.stop">;


inline std::array<uint8, 8> serveOrder{};
inline std::size_t serveOrderCount = 0;
inline ServeStopTicket heldServeStop;
inline bool serveStopHeld = false;
inline std::size_t serveResetCount = 0;
inline std::size_t serveKickCount = 0;
inline uint8 serveKickCode = 0;


void RecordServe(uint8 step) {
	if (serveOrderCount < serveOrder.size()) {
		serveOrder[serveOrderCount] = step;
	}

	serveOrderCount++;
}


void OnServeStop(ServeStopTicket ticket) {
	RecordServe(1);
	heldServeStop = ticket;
	serveStopHeld = true;
}


void OnServeReset() {
	RecordServe(2);
	serveResetCount++;
}


void OnServeKick(const Motor::MotorFault& fault) {
	RecordServe(3);
	serveKickCount++;
	serveKickCode = fault.code;
}


using ServeHandlers = VUB::Handlers<
	VUB::On<"motor.stop", &OnServeStop>,
	VUB::On<"motor.reset", &OnServeReset>,
	VUB::On<"motor.kick", &OnServeKick>>;


// The responder alone, with one ticket: the stop takes it, so a second stop is refused busy.
using ServeLayer = VUB::RequestsOf<ServeCommands, ServeHandlers, VUB::RequestsOptions{0, 1, 4, false}>;


void ResetServeState() {
	serveOrderCount = 0;
	serveResetCount = 0;
	serveKickCount = 0;
	serveKickCode = 0;
	serveStopHeld = false;
}


// A fire and a no-reply take no ticket. With the only ticket held by a stop, a no-reply runs at once, after the stop of the same burst; a fire
// runs at once with its body; a second stop finds no ticket and is refused busy, and nothing else is sent in that Process.
void TestFireAndNoReplyTakeNoTicket() {
	ResetServeState();
	ServeLayer layer;
	Probe probe;
	std::array<uint8, 5> stopOne = {0x01, 0x13, 0x01, 0x00, 0x01};
	SIM_EXPECT(layer.Take(VUB::Meta(), std::span<const uint8>(stopOne.data(), stopOne.size())).IsErr() == false);
	std::array<uint8, 5> resetTwo = {0x01, 0x14, 0x02, 0x00, 0x02};
	SIM_EXPECT(layer.Take(VUB::Meta(), std::span<const uint8>(resetTwo.data(), resetTwo.size())).IsErr() == false);
	SIM_EXPECT(serveStopHeld);
	SIM_EXPECT(serveResetCount == 1);
	SIM_EXPECT(serveOrderCount == 2);
	SIM_EXPECT(serveOrder[0] == 1);
	SIM_EXPECT(serveOrder[1] == 2);
	std::array<uint8, 5> stopThree = {0x01, 0x13, 0x03, 0x00, 0x03};
	SIM_EXPECT(layer.Take(VUB::Meta(), std::span<const uint8>(stopThree.data(), stopThree.size())).IsErr() == false);
	std::array<uint8, 6> kickFour = {0x01, 0x15, 0x04, 0x00, 0x04, 0x09};
	SIM_EXPECT(layer.Take(VUB::Meta(), std::span<const uint8>(kickFour.data(), kickFour.size())).IsErr() == false);
	SIM_EXPECT(serveKickCount == 1);
	SIM_EXPECT(serveKickCode == 9);
	layer.Process(1000, ProbeOutbox(probe));
	std::array<uint8, 5> busyThree = {0x03, 0x03, 0x00, 0x03, 0x04};
	SIM_EXPECT(probe.count == 1);
	SIM_EXPECT(ProbeLastIs(probe, busyThree));
	SIM_EXPECT(heldServeStop.Open());
	SIM_EXPECT(heldServeStop.Finish(Motor::MotorStatus::started) == ResultStatus::ok);
	layer.Process(2000, ProbeOutbox(probe));
	SIM_EXPECT(probe.count == 2);
}


// A bad body of a fire is returned as invalidData, with the handler not run; a good one runs the handler inside Take.
void TestFireBodyIsChecked() {
	ResetServeState();
	ServeLayer layer;
	Probe probe;
	std::array<uint8, 5> emptyKick = {0x01, 0x15, 0x05, 0x00, 0x05};
	SIM_EXPECT(layer.Take(VUB::Meta(), std::span<const uint8>(emptyKick.data(), emptyKick.size())).IsErr());
	SIM_EXPECT(serveKickCount == 0);
	layer.Process(1000, ProbeOutbox(probe));
	SIM_EXPECT(probe.count == 0);
}


// The final handler of a request sends the next request from inside the handler, with one slot: the slot is freed before the handler runs.
using ChainLayer = VUB::RequestsOf<RequesterCommands, VUB::Handlers<>, VUB::RequestsOptions{1, 0, 0, false}>;


inline ChainLayer::Handle* chainHandle = nullptr;
inline std::size_t chainFinals = 0;
inline ResultStatus chainResult = ResultStatus::notAvailable;
inline uint16 chainNext = 0;


void OnChainFinal(const VUB::Final<Motor::MotorStatus, VUB::NoBody>& reply) {
	chainFinals++;
	chainResult = reply.result;
	if (chainFinals == 1 && chainHandle != nullptr) {
		auto next = chainHandle->template Send<"motor.stop", &OnChainFinal>(VUB::NoBody{});
		if (next.IsErr() == false) {
			chainNext = next.Value().number;
		}
	}
}


void TestFinalHandlerSendsNextRequest() {
	chainFinals = 0;
	chainNext = 0;
	chainResult = ResultStatus::notAvailable;
	ChainLayer layer;
	Probe probe;
	ChainLayer::Handle handle(layer, ProbeOutbox(probe));
	chainHandle = &handle;
	auto first = handle.template Send<"motor.stop", &OnChainFinal>(VUB::NoBody{});
	SIM_EXPECT(first.IsErr() == false);
	std::array<uint8, 6> finalOne = FinalOk(1, 1);
	SIM_EXPECT(layer.Take(VUB::Meta(), std::span<const uint8>(finalOne.data(), finalOne.size())).IsErr() == false);
	// The handler ran inside Take: its own request took the freed slot, with number 2 and tag 2.
	SIM_EXPECT(chainFinals == 1);
	SIM_EXPECT(chainResult == ResultStatus::ok);
	SIM_EXPECT(chainNext == 2);
	SIM_EXPECT(probe.count == 2);
	std::array<uint8, 5> stopTwo = {0x01, 0x13, 0x02, 0x00, 0x02};
	SIM_EXPECT(ProbeLastIs(probe, stopTwo));
	std::array<uint8, 6> finalTwo = FinalOk(2, 2);
	SIM_EXPECT(layer.Take(VUB::Meta(), std::span<const uint8>(finalTwo.data(), finalTwo.size())).IsErr() == false);
	SIM_EXPECT(chainFinals == 2);
	chainHandle = nullptr;
}


// The interim handler of a request sends another request: the interim is taken, the handler sends, and the new request takes the next number.
using ChainLayerTwo = VUB::RequestsOf<RequesterCommands, VUB::Handlers<>, VUB::RequestsOptions{2, 0, 0, false}>;


inline ChainLayerTwo::Handle* chainHandleTwo = nullptr;
inline std::size_t chainInterims = 0;
inline uint16 chainInterimNext = 0;


void OnChainStartFinal(const VUB::Final<Motor::MotorStatus, Motor::MotorReport>&) {
}


void OnChainStopFinal(const VUB::Final<Motor::MotorStatus, VUB::NoBody>&) {
}


void OnChainInterim(const VUB::Progress<Motor::MotorStatus>&) {
	chainInterims++;
	if (chainInterims == 1 && chainHandleTwo != nullptr) {
		auto next = chainHandleTwo->template Send<"motor.stop", &OnChainStopFinal>(VUB::NoBody{});
		if (next.IsErr() == false) {
			chainInterimNext = next.Value().number;
		}
	}
}


void TestInterimHandlerSendsNextRequest() {
	chainInterims = 0;
	chainInterimNext = 0;
	ChainLayerTwo layer;
	Probe probe;
	ChainLayerTwo::Handle handle(layer, ProbeOutbox(probe));
	chainHandleTwo = &handle;
	auto start = handle.template Send<"motor.start", &OnChainStartFinal, &OnChainInterim>(Motor::MotorStart{});
	SIM_EXPECT(start.IsErr() == false);
	std::array<uint8, 6> interimOne = {0x02, 0x01, 0x00, 0x01, 0x01, 0x01};
	SIM_EXPECT(layer.Take(VUB::Meta(), std::span<const uint8>(interimOne.data(), interimOne.size())).IsErr() == false);
	SIM_EXPECT(chainInterims == 1);
	SIM_EXPECT(chainInterimNext == 2);
	SIM_EXPECT(probe.count == 2);
	std::array<uint8, 5> stopTwo = {0x01, 0x13, 0x02, 0x00, 0x02};
	SIM_EXPECT(ProbeLastIs(probe, stopTwo));
	chainHandleTwo = nullptr;
}


inline std::size_t tenCount = 0;
inline std::array<uint8, 16> tenStatuses{};


void OnTenInterim(const VUB::Progress<Motor::MotorStatus>& progress) {
	if (tenCount < tenStatuses.size()) {
		tenStatuses[tenCount] = static_cast<uint8>(progress.status);
	}

	tenCount++;
}


// Ten interim replies taken in one burst: each handler runs at once, in arrival order, and none is lost.
void TestTenInterimsInOneProcess() {
	tenCount = 0;
	tenStatuses.fill(0);
	ChainLayerTwo layer;
	Probe probe;
	ChainLayerTwo::Handle handle(layer, ProbeOutbox(probe));
	auto start = handle.template Send<"motor.start", &OnChainStartFinal, &OnTenInterim>(Motor::MotorStart{});
	SIM_EXPECT(start.IsErr() == false);
	for (uint8 index = 0; index < 10; index++) {
		std::array<uint8, 6> interim = {0x02, 0x01, 0x00, 0x01, static_cast<uint8>(index + 1), static_cast<uint8>(10 + index)};
		SIM_EXPECT(layer.Take(VUB::Meta(), std::span<const uint8>(interim.data(), interim.size())).IsErr() == false);
	}

	layer.Process(1000, ProbeOutbox(probe));
	SIM_EXPECT(tenCount == 10);
	for (std::size_t index = 0; index < 10; index++) {
		SIM_EXPECT(tenStatuses[index] == static_cast<uint8>(10 + index));
	}
}


// The requester of a stack, with one slot and an in-order Reliable below it: the chain of the final handler's test over that layer.
using InOrderRequester = VUB::Stack<+[]() {
	return VUB::Builder()
		.Layer<VUB::Requests<RequesterCommands, VUB::Handlers<>>>().Open(1)
		.Layer<VUB::ReliableOf<40, 4, true>>().Retries(3).TimeoutMs(200).Window(4)
		.Link<ReportingPacketEnd>()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


using InOrderHandle = decltype(std::declval<InOrderRequester&>().Requests());


inline InOrderHandle* inOrderHandle = nullptr;
inline std::size_t inOrderFinals = 0;
inline uint16 inOrderNext = 0;


void OnInOrderFinal(const VUB::Final<Motor::MotorStatus, VUB::NoBody>& reply) {
	inOrderFinals++;
	if (inOrderFinals == 1 && inOrderHandle != nullptr) {
		auto next = inOrderHandle->template Send<"motor.stop", &OnInOrderFinal>(VUB::NoBody{});
		if (next.IsErr() == false) {
			inOrderNext = next.Value().number;
		}
	}

	(void)reply;
}


// The same as the final-handler test over a stack, with the in-order Reliable: the second request goes out after the first final.
void TestFinalHandlerSendsNextRequestInOrder() {
	ResetRequestsState();
	ReportingPacketLine line(VUB::Faults{});
	InOrderRequester near(line.a);
	Responder far(line.b);
	WireLog log;
	near.template Find<VUB::ReliableOf<40, 4, true>>().SetSession(ReliableSession);
	GiveSession(far);
	stopAtOnce = true;
	RoundOf(line, near, far, log, 50);
	auto handle = near.Requests();
	auto probe = handle.template Send<"motor.stop", &OnInOrderFinal>(VUB::NoBody{});
	SIM_EXPECT(probe.IsErr());
	SIM_EXPECT(probe.Error() == ResultStatus::busy);
	std::array<uint8, 1> trigger = {0xEE};
	SIM_EXPECT(far.LayerOutbox<0>().Send(std::span<const uint8>(trigger.data(), trigger.size())) == ResultStatus::busy);
	for (uint32 round = 0; round < 3; round++) {
		RoundOf(line, near, far, log, 100 + round);
	}

	inOrderHandle = &handle;
	auto first = handle.template Send<"motor.stop", &OnInOrderFinal>(VUB::NoBody{});
	SIM_EXPECT(first.IsErr() == false);
	for (uint32 round = 0; round < 6; round++) {
		RoundOf(line, near, far, log, 1000 + round * 10);
	}

	SIM_EXPECT(inOrderFinals == 2);
	SIM_EXPECT(inOrderNext == 2);
	SIM_EXPECT(stopsSeen == 2);
	inOrderHandle = nullptr;
}


// A plain chain: no delivery layer below the requests. The request is the bare packet, and its reply is the bare final.
using PlainRequester = VUB::Stack<+[]() {
	return VUB::Builder()
		.Layer<VUB::Requests<RequesterCommands, VUB::Handlers<>>>().Open(4)
		.Link<ReportingPacketEnd>()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


using PlainResponder = VUB::Stack<+[]() {
	return VUB::Builder()
		.Layer<VUB::Requests<Motor::MotorCommands, DeviceHandlers>>().Tickets(2)
		.Link<ReportingPacketEnd>()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


void TestPlainReplyAtOnce() {
	ResetRequestsState();
	stopAtOnce = true;
	ReportingPacketLine line(VUB::Faults{});
	PlainRequester near(line.a);
	PlainResponder far(line.b);
	WireLog log;
	RoundOf(line, near, far, log, 50);
	auto handle = near.Requests();
	SIM_EXPECT(SendStopR(handle));
	RoundOf(line, near, far, log, 1000);
	SIM_EXPECT(stopsSeen == 1);
	SIM_EXPECT(stopFinals == 1);
	SIM_EXPECT(stopResult == ResultStatus::ok);
	std::array<uint8, 5> request = {0x01, 0x13, 0x01, 0x00, 0x01};
	SIM_EXPECT(log.toFar.count == 1);
	SIM_EXPECT(CaptureIs(log.toFar, 0, request));
	std::array<uint8, 6> finalStarted = {0x03, 0x01, 0x00, 0x01, 0x00, 0x00};
	SIM_EXPECT(log.toNear.count == 1);
	SIM_EXPECT(CaptureIs(log.toNear, 0, finalStarted));
}


void TestPlainInterimAndFinal() {
	ResetRequestsState();
	startInterimAtOnce = true;
	ReportingPacketLine line(VUB::Faults{});
	PlainRequester near(line.a);
	PlainResponder far(line.b);
	WireLog log;
	auto handle = near.Requests();
	SIM_EXPECT(SendStartR(handle));
	RoundOf(line, near, far, log, 1000);
	SIM_EXPECT(startInterims == 1);
	SIM_EXPECT(lastInterimStatus == Motor::MotorStatus::warming);
	SIM_EXPECT(startFinals == 0);
	SIM_EXPECT(heldStart.Open());
	Motor::MotorReport report{};
	report.speedRpm = 1200;
	report.currentMa = 850;
	SIM_EXPECT(heldStart.Finish(Motor::MotorStatus::started, report) == ResultStatus::ok);
	RoundOf(line, near, far, log, 2000);
	SIM_EXPECT(startFinals == 1);
	SIM_EXPECT(startResult == ResultStatus::ok);
	SIM_EXPECT(startReport.speedRpm == 1200);
}


void TestPlainTimeout() {
	ResetRequestsState();
	ReportingPacketLine line(VUB::Faults{});
	PlainRequester near(line.a);
	PlainResponder far(line.b);
	WireLog log;
	RoundOf(line, near, far, log, 50);
	auto handle = near.Requests();
	SIM_EXPECT(SendStopR(handle));
	uint32 now = 1000;
	for (int step = 0; step < 25 && stopFinals == 0; step++) {
		RoundOf(line, near, far, log, now);
		now += 100000;
	}

	SIM_EXPECT(stopFinals == 1);
	SIM_EXPECT(stopResult == ResultStatus::timeout);
}


void TestPlainRefusal() {
	ResetRequestsState();
	ReportingPacketLine line(VUB::Faults{});
	PlainRequester near(line.a);
	PlainResponder far(line.b);
	WireLog log;
	auto handle = near.Requests();
	auto unknown = handle.template Send<"motor.unknown", &OnUnknownFinalR>(VUB::NoBody{});
	SIM_EXPECT(unknown.IsErr() == false);
	RoundOf(line, near, far, log, 1000);
	SIM_EXPECT(unknownFinals == 1);
	SIM_EXPECT(unknownResult == ResultStatus::notSupported);
}


// A fire over the plain chain: one packet with no acknowledgement, and the responder's handler runs when the packet is taken.
using PlainServeRequester = VUB::Stack<+[]() {
	return VUB::Builder()
		.Layer<VUB::Requests<ServeCommands, VUB::Handlers<>>>().Open(1)
		.Link<ReportingPacketEnd>()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


using PlainServeResponder = VUB::Stack<+[]() {
	return VUB::Builder()
		.Layer<VUB::Requests<ServeCommands, ServeHandlers>>().Tickets(1)
		.Link<ReportingPacketEnd>()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


void TestPlainFire() {
	ResetServeState();
	ReportingPacketLine line(VUB::Faults{});
	PlainServeRequester near(line.a);
	PlainServeResponder far(line.b);
	WireLog log;
	auto handle = near.Requests();
	SIM_EXPECT(handle.template Fire<"motor.kick">(Motor::MotorFault{7}) == ResultStatus::ok);
	RoundOf(line, near, far, log, 1000);
	SIM_EXPECT(serveKickCount == 1);
	SIM_EXPECT(serveKickCode == 7);
	std::array<uint8, 6> kick = {0x01, 0x15, 0x01, 0x00, 0x01, 0x07};
	SIM_EXPECT(log.toFar.count == 1);
	SIM_EXPECT(CaptureIs(log.toFar, 0, kick));
}


// A link that has not come up yet: the first Process has not sampled it, so a FailWhileDown send is refused with noConnection before it reaches
// the layer below. A send of a command without FailWhileDown is refused by the layer below (noInit: no session has a boot value).
void TestSendBeforeLinkUpIsNoConnection() {
	ResetRequestsState();
	ReportingPacketLine line(VUB::Faults{});
	Requester near(line.a);
	auto handle = near.Requests();
	auto refused = handle.Send<"motor.stop", &OnStopFinalR>(VUB::NoBody{});
	SIM_EXPECT(refused.IsErr());
	SIM_EXPECT(refused.Error() == ResultStatus::noConnection);
	auto unrestricted = handle.Send<"motor.start", &OnStartFinalR>(Motor::MotorStart{});
	SIM_EXPECT(unrestricted.IsErr());
	SIM_EXPECT(unrestricted.Error() == ResultStatus::noInit);
}


// A request its requester closed while the link was down is not sent again when the link comes back: the request is on the wire and lost,
// its deadline ends it while the link is down, and the layer's answer for it is no. The responder's handler does not run.
void TestTimedOutRequestNotResent() {
	ResetRequestsState();
	ReportingPacketLine line(VUB::Faults{});
	Requester near(line.a);
	Responder far(line.b);
	WireLog log;
	Establish(line, near, far, log);
	auto handle = near.Requests();
	SIM_EXPECT(SendStartR(handle));
	testNowUs = 1000;
	line.SetNow(1000);
	near.Process();
	LostSink lost;
	line.DeliverToB(lost);
	line.SetLinkUp(false);
	uint32 now = 2000;
	for (int step = 0; step < 60 && startFinals == 0; step++) {
		testNowUs = now;
		line.SetNow(now);
		near.Process();
		now += 100000;
	}

	SIM_EXPECT(startFinals == 1);
	SIM_EXPECT(startResult == ResultStatus::timeout);
	line.SetLinkUp(true);
	for (int round = 0; round < 4; round++) {
		Run(line, near, far, log, now);
		now += 1000;
	}

	SIM_EXPECT(startsSeen == 0);
	SIM_EXPECT(startFinals == 1);
}


// The same for a FailWhileDown command: the link's loss closes it with noConnection, and on up its lost packet is not sent again.
void TestFailWhileDownRequestNotResent() {
	ResetRequestsState();
	ReportingPacketLine line(VUB::Faults{});
	Requester near(line.a);
	Responder far(line.b);
	WireLog log;
	Establish(line, near, far, log);
	auto handle = near.Requests();
	SIM_EXPECT(SendStopR(handle));
	testNowUs = 1000;
	line.SetNow(1000);
	near.Process();
	LostSink lost;
	line.DeliverToB(lost);
	line.SetLinkUp(false);
	testNowUs = 2000;
	line.SetNow(2000);
	near.Process();
	near.Process();
	SIM_EXPECT(stopFinals == 1);
	SIM_EXPECT(stopResult == ResultStatus::noConnection);
	line.SetLinkUp(true);
	for (uint32 round = 0; round < 4; round++) {
		Run(line, near, far, log, 3000 + round * 1000);
	}

	SIM_EXPECT(stopsSeen == 0);
	SIM_EXPECT(stopFinals == 1);
}


// A request still open while the link is down is kept: its packet is sent again when the link is up, and the responder answers it.
void TestOpenRequestResentOnUp() {
	ResetRequestsState();
	startAtOnce = true;
	ReportingPacketLine line(VUB::Faults{});
	Requester near(line.a);
	Responder far(line.b);
	WireLog log;
	Establish(line, near, far, log);
	auto handle = near.Requests();
	SIM_EXPECT(SendStartR(handle));
	testNowUs = 1000;
	line.SetNow(1000);
	near.Process();
	LostSink lost;
	line.DeliverToB(lost);
	line.SetLinkUp(false);
	for (uint32 now = 2000; now <= 1000000; now += 1000) {
		testNowUs = now;
		line.SetNow(now);
		near.Process();
	}

	line.SetLinkUp(true);
	for (uint32 round = 0; round < 4; round++) {
		Run(line, near, far, log, 1000000 + round * 1000);
	}

	SIM_EXPECT(startsSeen == 1);
	SIM_EXPECT(startFinals == 1);
	SIM_EXPECT(startResult == ResultStatus::ok);
}


int main() {
	TestReplyAtOnce();
	TestReplyLater();
	TestInterimReplies();
	TestRequesterTimeoutAndLateFinal();
	TestRetryExhausted();
	TestTicketTimeout();
	TestPeerRestartClosesOpenRequests();
	TestLinkDown();
	TestRefusals();
	TestFinalWithReport();
	TestFullTableAndBusySend();
	TestNumberWrapWithLateFinal();
	TestFireAndNoReplyTakeNoTicket();
	TestFireBodyIsChecked();
	TestFinalHandlerSendsNextRequest();
	TestInterimHandlerSendsNextRequest();
	TestTenInterimsInOneProcess();
	TestFinalHandlerSendsNextRequestInOrder();
	TestPlainReplyAtOnce();
	TestPlainInterimAndFinal();
	TestPlainTimeout();
	TestPlainRefusal();
	TestPlainFire();
	TestSendBeforeLinkUpIsNoConnection();
	TestTimedOutRequestNotResent();
	TestFailWhileDownRequestNotResent();
	TestOpenRequestResentOnUp();
	return SimFinish("TestVubRequests");
}
