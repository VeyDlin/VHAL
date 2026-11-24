#pragma once
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

// The common part of the requests tests: the command sets, the device's handlers and the requester's recorders, the stacks over a Reliable
// link, the rounds that run them, and the layer-only probes and responder commands. Each requests test program includes it once.
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


// The full table and the layer-only probes: a requester layer with a recording outbox, and the packets it sends and takes are hand-written.
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


void ResetServeState() {
	serveOrderCount = 0;
	serveResetCount = 0;
	serveKickCount = 0;
	serveKickCode = 0;
	serveStopHeld = false;
}


// A requester with two slots and no delivery layer below it: the chains of the interim and the ten-interim tests use it.
using ChainLayerTwo = VUB::RequestsOf<RequesterCommands, VUB::Handlers<>, VUB::RequestsOptions{2, 0, 0, false}>;


void OnChainStartFinal(const VUB::Final<Motor::MotorStatus, Motor::MotorReport>&) {
}


void OnChainStopFinal(const VUB::Final<Motor::MotorStatus, VUB::NoBody>&) {
}
