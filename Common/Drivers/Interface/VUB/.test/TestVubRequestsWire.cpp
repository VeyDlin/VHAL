#include "RequestsMotor.h"
#include "SimCheck.h"
#include <Drivers/Interface/VUB/Outbox.h>
#include <Drivers/Interface/VUB/Requests.h>
#include <Utilities/DataTypes.h>
#include <array>
#include <cstddef>
#include <span>

// The requests layer's packets, checked byte for byte. The layer is driven without a stack: a recording outbox takes what the layer
// sends, and hand-written packets go in through Take. The expected bytes are written by hand from the wire layout (payloads ahead of Reliable's
// header).


// The device's handlers. They keep what they were given, so a test can check it and finish the ticket itself.
using StartTicket = VUB::Ticket<Motor::MotorCommands, Motor::MotorCommand::start>;
using StopTicket = VUB::Ticket<Motor::MotorCommands, Motor::MotorCommand::stop>;


inline std::array<Motor::MotorStart, 16> seenStarts{};
inline std::size_t seenStartCount = 0;
inline StartTicket heldStart;
inline bool startHeld = false;
inline StopTicket heldStop;
inline bool stopHeld = false;
inline std::size_t resetCount = 0;


void OnStart(StartTicket ticket, const Motor::MotorStart& body) {
	if (seenStartCount < seenStarts.size()) {
		seenStarts[seenStartCount] = body;
	}

	seenStartCount++;
	heldStart = ticket;
	startHeld = true;
}


void OnStop(StopTicket ticket) {
	heldStop = ticket;
	stopHeld = true;
}


void OnReset() {
	resetCount++;
}


using DeviceHandlers = VUB::Handlers<
	VUB::On<Motor::MotorCommand::start, &OnStart>,
	VUB::On<Motor::MotorCommand::stop, &OnStop>,
	VUB::On<Motor::MotorCommand::reset, &OnReset>>;


// The requester's handlers: each final and interim is recorded.
inline std::size_t finalCount = 0;
inline ResultStatus lastFinalResult = ResultStatus::ok;
inline Motor::MotorStatus lastFinalStatus = Motor::MotorStatus::started;
inline Motor::MotorReport lastFinalReport{};
inline std::size_t interimCount = 0;
inline Motor::MotorStatus lastInterimStatus = Motor::MotorStatus::started;
inline std::size_t stopFinalCount = 0;
inline ResultStatus lastStopResult = ResultStatus::ok;
inline std::size_t resetFinalCount = 0;
inline ResultStatus lastResetResult = ResultStatus::ok;


void OnStartFinal(const VUB::Final<Motor::MotorStatus, Motor::MotorReport>& reply) {
	finalCount++;
	lastFinalResult = reply.result;
	lastFinalStatus = reply.status;
	lastFinalReport = reply.body;
}


void OnStartInterim(const VUB::Progress<Motor::MotorStatus>& progress) {
	interimCount++;
	lastInterimStatus = progress.status;
}


void OnStopFinal(const VUB::Final<Motor::MotorStatus, VUB::NoBody>& reply) {
	stopFinalCount++;
	lastStopResult = reply.result;
}


void OnResetDone(ResultStatus result) {
	resetFinalCount++;
	lastResetResult = result;
}


using RequesterHandlers = VUB::Handlers<>;


// The recording outbox: every packet the layer sends is kept with its flags, until the test clears it.
struct Sent {
	std::array<uint8, 64> bytes{};
	std::size_t length = 0;
	uint8 flags = 0;
};


// The first 16 packets are kept, and the last one always: count is the number of packets sent.
struct Recorder {
	std::array<Sent, 16> sent{};
	Sent last{};
	std::size_t count = 0;
	ResultStatus answer = ResultStatus::ok;
};


void CopyPacket(Sent& target, std::span<const uint8> packet, const VUB::Meta& meta) {
	for (std::size_t index = 0; index < packet.size() && index < target.bytes.size(); index++) {
		target.bytes[index] = packet[index];
	}

	target.length = packet.size();
	target.flags = meta.flags;
}


ResultStatus RecordSend(void* owner, std::span<const uint8> packet, const VUB::Meta& meta) {
	Recorder* recorder = static_cast<Recorder*>(owner);
	if (recorder->answer != ResultStatus::ok) {
		return recorder->answer;
	}

	if (recorder->count < recorder->sent.size()) {
		CopyPacket(recorder->sent[recorder->count], packet, meta);
	}

	CopyPacket(recorder->last, packet, meta);
	recorder->count++;
	return ResultStatus::ok;
}


VUB::Outbox OutboxOf(Recorder& recorder) {
	return VUB::Outbox(&recorder, &RecordSend, nullptr, nullptr);
}


// A motor.start with its final and interim handlers, and a motor.stop with its final: true when the request was accepted.
template<typename HandleType>
bool SendStart(HandleType& handle, const Motor::MotorStart& body) {
	auto sent = handle.template Send<Motor::MotorCommand::start, &OnStartFinal, &OnStartInterim>(body);
	return sent.IsErr() == false;
}


template<typename HandleType>
bool SendStop(HandleType& handle) {
	auto sent = handle.template Send<Motor::MotorCommand::stop, &OnStopFinal>(VUB::NoBody{});
	return sent.IsErr() == false;
}


// True when the sent packet index is exactly the expected bytes.
template<std::size_t Size>
bool SentIs(const Recorder& recorder, std::size_t index, const std::array<uint8, Size>& expected) {
	if (index >= recorder.count || index >= recorder.sent.size() || recorder.sent[index].length != Size) {
		return false;
	}

	for (std::size_t byte = 0; byte < Size; byte++) {
		if (recorder.sent[index].bytes[byte] != expected[byte]) {
			return false;
		}
	}

	return true;
}


// The layer of the device's requester and responder over Reliable: eight requester slots, two tickets, the default refusal ring.
using Layer = VUB::RequestsOf<Motor::MotorCommands, DeviceHandlers, VUB::RequestsOptions{8, 2, 4, true}>;


// The same layer with no Reliable below, which takes its session from the handle.
using SessionLayer = VUB::RequestsOf<Motor::MotorCommands, DeviceHandlers, VUB::RequestsOptions{8, 2, 4}>;


// A responder with one ticket, for the no-free-ticket refusal.
using OneTicketLayer = VUB::RequestsOf<Motor::MotorCommands, DeviceHandlers, VUB::RequestsOptions{0, 1, 4}>;


// A command with no reply, for the fire: motor.reset is NoReply, which has a delivery outcome, so its fire is a separate set.
using FireCommands = VUB::CommandSet<+[]() {
	return VUB::Commands()
		.Command<Motor::MotorCommand::reset>()
			.Fire();
}>;


using FireHandlers = VUB::Handlers<
	VUB::On<Motor::MotorCommand::reset, &OnReset>>;


using FireLayer = VUB::RequestsOf<FireCommands, FireHandlers, VUB::RequestsOptions{0, 1, 0}>;


void ResetWireState() {
	seenStartCount = 0;
	startHeld = false;
	stopHeld = false;
	resetCount = 0;
	finalCount = 0;
	interimCount = 0;
	stopFinalCount = 0;
	resetFinalCount = 0;
	lastFinalResult = ResultStatus::ok;
	lastStopResult = ResultStatus::ok;
	lastResetResult = ResultStatus::ok;
}


// The layer's requester: the bytes of the requests and the answers they get. Six starts, then a stop, give the numbers 1 to 8 with the same
// tag: number 7 carries the ramp of the example, and number 8 is the stop.
void TestRequestBytes() {
	ResetWireState();
	Layer layer;
	Recorder recorder;
	Layer::Handle handle(layer, OutboxOf(recorder));

	for (int round = 0; round < 6; round++) {
		Motor::MotorStart start{};
		SIM_EXPECT(SendStart(handle, start));
	}

	Motor::MotorStart seventh{};
	seventh.speedRpm = 1200;
	seventh.direction = 1;
	SIM_EXPECT(SendStart(handle, seventh));
	SIM_EXPECT(SendStop(handle));

	SIM_EXPECT(recorder.count == 8);
	// motor.start, number 1, a zero start: the ramp is the default 300 (0x012C), so the body is written in full.
	std::array<uint8, 10> firstStart = {0x01, 0x12, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x2C, 0x01};
	SIM_EXPECT(SentIs(recorder, 0, firstStart));
	//motor.start, number 7, MotorStart{1200, 1} (v2, ramp 300 by default).
	std::array<uint8, 10> startSeven = {0x01, 0x12, 0x07, 0x00, 0x07, 0xB0, 0x04, 0x01, 0x2C, 0x01};
	SIM_EXPECT(SentIs(recorder, 6, startSeven));
	//motor.stop, number 8 (no body).
	std::array<uint8, 5> stopEight = {0x01, 0x13, 0x08, 0x00, 0x08};
	SIM_EXPECT(SentIs(recorder, 7, stopEight));
	SIM_EXPECT(recorder.sent[7].flags == 0);
}


// The answers of the device, from the wire: an interim (warming) for number 7, then the final with the report. Then a final of number 6
// with operationCancelled, a final of number 8 with status only, and a second final for number 8, which is dropped.
void TestAnswerBytes() {
	ResetWireState();
	Layer layer;
	Recorder recorder;
	Layer::Handle handle(layer, OutboxOf(recorder));
	for (int round = 0; round < 6; round++) {
		Motor::MotorStart start{};
		SIM_EXPECT(SendStart(handle, start));
	}

	Motor::MotorStart seventh{};
	seventh.speedRpm = 1200;
	seventh.direction = 1;
	SIM_EXPECT(SendStart(handle, seventh));
	SIM_EXPECT(SendStop(handle));

	//interim warming for number 7, index 1.
	std::array<uint8, 6> interimSeven = {0x02, 0x07, 0x00, 0x07, 0x01, 0x01};
	SIM_EXPECT(layer.Take(VUB::Meta(), std::span<const uint8>(interimSeven.data(), interimSeven.size())).IsErr() == false);
	std::array<uint8, 10> finalSeven = {0x03, 0x07, 0x00, 0x07, 0x00, 0x00, 0xB0, 0x04, 0x52, 0x03};
	SIM_EXPECT(layer.Take(VUB::Meta(), std::span<const uint8>(finalSeven.data(), finalSeven.size())).IsErr() == false);
	layer.Process(1000, OutboxOf(recorder));
	SIM_EXPECT(interimCount == 1);
	SIM_EXPECT(lastInterimStatus == Motor::MotorStatus::warming);
	SIM_EXPECT(finalCount == 1);
	SIM_EXPECT(lastFinalResult == ResultStatus::ok);
	SIM_EXPECT(lastFinalStatus == Motor::MotorStatus::started);
	SIM_EXPECT(lastFinalReport.speedRpm == 1200);
	SIM_EXPECT(lastFinalReport.currentMa == 850);

	//final operationCancelled for number 6.
	std::array<uint8, 5> cancelledSix = {0x03, 0x06, 0x00, 0x06, 0x01};
	SIM_EXPECT(layer.Take(VUB::Meta(), std::span<const uint8>(cancelledSix.data(), cancelledSix.size())).IsErr() == false);
	//final started for number 8 (status only).
	std::array<uint8, 6> stopFinal = {0x03, 0x08, 0x00, 0x08, 0x00, 0x00};
	SIM_EXPECT(layer.Take(VUB::Meta(), std::span<const uint8>(stopFinal.data(), stopFinal.size())).IsErr() == false);
	// A second final of number 7 is late: the request closed, so it is dropped and the handler is not called again.
	SIM_EXPECT(layer.Take(VUB::Meta(), std::span<const uint8>(finalSeven.data(), finalSeven.size())).IsErr() == false);
	layer.Process(2000, OutboxOf(recorder));
	SIM_EXPECT(finalCount == 2);
	SIM_EXPECT(lastFinalResult == ResultStatus::operationCancelled);
	SIM_EXPECT(stopFinalCount == 1);
	SIM_EXPECT(lastStopResult == ResultStatus::ok);
}


// The device side from the wire: a v1 peer's motor.start has no ramp field, so the default applies. The device's interim and final are
// packets of the layer, in that order.
void TestResponderBytes() {
	ResetWireState();
	Layer layer;
	Recorder recorder;
	std::array<uint8, 8> v1Start = {0x01, 0x12, 0x07, 0x00, 0x07, 0xB0, 0x04, 0x01};
	SIM_EXPECT(layer.Take(VUB::Meta(), std::span<const uint8>(v1Start.data(), v1Start.size())).IsErr() == false);
	layer.Process(1000, OutboxOf(recorder));
	SIM_EXPECT(seenStartCount == 1);
	SIM_EXPECT(seenStarts[0].speedRpm == 1200);
	SIM_EXPECT(seenStarts[0].direction == 1);
	SIM_EXPECT(seenStarts[0].rampMs == 300);
	SIM_EXPECT(startHeld);
	SIM_EXPECT(recorder.count == 0);

	//the interim warming and the final with the report, both for number 7, in that order.
	SIM_EXPECT(heldStart.Interim(Motor::MotorStatus::warming) == ResultStatus::ok);
	Motor::MotorReport report{};
	report.speedRpm = 1200;
	report.currentMa = 850;
	SIM_EXPECT(heldStart.Finish(Motor::MotorStatus::started, report) == ResultStatus::ok);
	SIM_EXPECT(heldStart.Open() == false);
	layer.Process(1100, OutboxOf(recorder));
	std::array<uint8, 6> interim = {0x02, 0x07, 0x00, 0x07, 0x01, 0x01};
	std::array<uint8, 10> final = {0x03, 0x07, 0x00, 0x07, 0x00, 0x00, 0xB0, 0x04, 0x52, 0x03};
	SIM_EXPECT(recorder.count == 2);
	SIM_EXPECT(SentIs(recorder, 0, interim));
	SIM_EXPECT(SentIs(recorder, 1, final));
	// The ticket is closed: a second finish is notAvailable and nothing more is sent.
	SIM_EXPECT(heldStart.Open() == false);
	SIM_EXPECT(heldStart.Finish(Motor::MotorStatus::failed, report) == ResultStatus::notAvailable);
	layer.Process(1200, OutboxOf(recorder));
	SIM_EXPECT(recorder.count == 2);
}


// Refusals from the ring, decided in Take and sent from Process: an unknown command id is code 2, a bad body is code 3, and a request
// with no free ticket is code 4 (busy). The ticket of the first stop is held by its handler, so the second stop has no ticket.
void TestRefusalBytes() {
	ResetWireState();
	OneTicketLayer layer;
	Recorder recorder;
	std::array<uint8, 5> stopTen = {0x01, 0x13, 0x0A, 0x00, 0x0A};
	SIM_EXPECT(layer.Take(VUB::Meta(), std::span<const uint8>(stopTen.data(), stopTen.size())).IsErr() == false);
	// Unknown id 0x77, number 11: refused as notSupported (code 2).
	std::array<uint8, 5> unknownEleven = {0x01, 0x77, 0x0B, 0x00, 0x0B};
	SIM_EXPECT(layer.Take(VUB::Meta(), std::span<const uint8>(unknownEleven.data(), unknownEleven.size())).IsErr() == false);
	// motor.stop, number 12: no free ticket (code 4).
	std::array<uint8, 5> stopTwelve = {0x01, 0x13, 0x0C, 0x00, 0x0C};
	SIM_EXPECT(layer.Take(VUB::Meta(), std::span<const uint8>(stopTwelve.data(), stopTwelve.size())).IsErr() == false);
	layer.Process(1000, OutboxOf(recorder));
	std::array<uint8, 5> refusedEleven = {0x03, 0x0B, 0x00, 0x0B, 0x02};
	std::array<uint8, 5> refusedTwelve = {0x03, 0x0C, 0x00, 0x0C, 0x04};
	SIM_EXPECT(recorder.count == 2);
	SIM_EXPECT(SentIs(recorder, 0, refusedEleven));
	SIM_EXPECT(SentIs(recorder, 1, refusedTwelve));
	SIM_EXPECT(stopHeld);

	// A bad body: motor.start with one byte where speedRpm and direction are required (code 3). The ticket is taken by the stop, so this is
	// checked on a layer with tickets.
	ResetWireState();
	Layer bodyLayer;
	Recorder bodyRecorder;
	std::array<uint8, 6> shortStart = {0x01, 0x12, 0x0D, 0x00, 0x0D, 0x01};
	SIM_EXPECT(bodyLayer.Take(VUB::Meta(), std::span<const uint8>(shortStart.data(), shortStart.size())).IsErr() == false);
	bodyLayer.Process(1000, OutboxOf(bodyRecorder));
	std::array<uint8, 5> refusedThirteen = {0x03, 0x0D, 0x00, 0x0D, 0x03};
	SIM_EXPECT(bodyRecorder.count == 1);
	SIM_EXPECT(SentIs(bodyRecorder, 0, refusedThirteen));
	SIM_EXPECT(seenStartCount == 0);
}


// A request whose number is open on the responder: the same tag is a duplicate and is ignored; another tag is refused busy with its
// own tag, and the open ticket is not touched.
void TestOpenNumberBytes() {
	ResetWireState();
	Layer layer;
	Recorder recorder;
	std::array<uint8, 5> stopSeven = {0x01, 0x13, 0x07, 0x00, 0x07};
	SIM_EXPECT(layer.Take(VUB::Meta(), std::span<const uint8>(stopSeven.data(), stopSeven.size())).IsErr() == false);
	std::array<uint8, 5> duplicateSeven = {0x01, 0x13, 0x07, 0x00, 0x07};
	SIM_EXPECT(layer.Take(VUB::Meta(), std::span<const uint8>(duplicateSeven.data(), duplicateSeven.size())).IsErr() == false);
	std::array<uint8, 5> otherTagSeven = {0x01, 0x13, 0x07, 0x00, 0x08};
	SIM_EXPECT(layer.Take(VUB::Meta(), std::span<const uint8>(otherTagSeven.data(), otherTagSeven.size())).IsErr() == false);
	layer.Process(1000, OutboxOf(recorder));
	std::array<uint8, 5> busyEight = {0x03, 0x07, 0x00, 0x08, 0x04};
	SIM_EXPECT(recorder.count == 1);
	SIM_EXPECT(SentIs(recorder, 0, busyEight));
	SIM_EXPECT(stopHeld);
	SIM_EXPECT(heldStop.Open() == true);
	SIM_EXPECT(heldStop.Number().number == 7);
}


// The fire of a command with no reply: the packet carries the no-acknowledgement flag and no slot is used.
void TestFireBytes() {
	ResetWireState();
	FireLayer layer;
	Recorder recorder;
	FireLayer::Handle handle(layer, OutboxOf(recorder));
	SIM_EXPECT(handle.Fire<Motor::MotorCommand::reset>(VUB::NoBody{}) == ResultStatus::ok);
	std::array<uint8, 5> reset = {0x01, 0x14, 0x01, 0x00, 0x01};
	SIM_EXPECT(recorder.count == 1);
	SIM_EXPECT(SentIs(recorder, 0, reset));
	SIM_EXPECT(recorder.sent[0].flags == VUB::Meta::NoAcknowledgeFlag);
	SIM_EXPECT(layer.Take(VUB::Meta(), std::span<const uint8>(reset.data(), reset.size())).IsErr() == false);
	layer.Process(1000, OutboxOf(recorder));
	SIM_EXPECT(resetCount == 1);
	SIM_EXPECT(recorder.count == 1);
}


// A handler set with no command that has a reply needs no tickets: the layer builds with the default Tickets(0), and a fire runs its handler.
using FireOnlyLayer = VUB::RequestsOf<FireCommands, FireHandlers>;


void TestFireWithoutTickets() {
	ResetWireState();
	FireOnlyLayer layer;
	std::array<uint8, 5> reset = {0x01, 0x14, 0x03, 0x00, 0x03};
	SIM_EXPECT(layer.Take(VUB::Meta(), std::span<const uint8>(reset.data(), reset.size())).IsErr() == false);
	SIM_EXPECT(resetCount == 1);
}


// Malformed packets: an empty packet, an unknown kind, and a short header. Each is refused as an error of the layer, with no answer.
void TestMalformed() {
	ResetWireState();
	Layer layer;
	Recorder recorder;
	std::array<uint8, 1> unknownKind = {0x09};
	SIM_EXPECT(layer.Take(VUB::Meta(), std::span<const uint8>(unknownKind.data(), unknownKind.size())).IsErr());
	SIM_EXPECT(layer.Take(VUB::Meta(), std::span<const uint8>()).IsErr());
	std::array<uint8, 3> shortRequest = {0x01, 0x12, 0x07};
	SIM_EXPECT(layer.Take(VUB::Meta(), std::span<const uint8>(shortRequest.data(), shortRequest.size())).IsErr());
	std::array<uint8, 4> shortFinal = {0x03, 0x07, 0x00, 0x07};
	SIM_EXPECT(layer.Take(VUB::Meta(), std::span<const uint8>(shortFinal.data(), shortFinal.size())).IsErr());
	layer.Process(1000, OutboxOf(recorder));
	SIM_EXPECT(recorder.count == 0);
}


// A FINAL of code 0 with no status byte is an error of the layer: the request it names stays open, and the whole final closes it.
void TestFinalWithoutStatus() {
	ResetWireState();
	Layer layer;
	Recorder recorder;
	Layer::Handle handle(layer, OutboxOf(recorder));
	Motor::MotorStart start{};
	SIM_EXPECT(SendStart(handle, start));
	std::array<uint8, 5> noStatus = {0x03, 0x01, 0x00, 0x01, 0x00};
	SIM_EXPECT(layer.Take(VUB::Meta(), std::span<const uint8>(noStatus.data(), noStatus.size())).IsErr());
	SIM_EXPECT(finalCount == 0);
	std::array<uint8, 10> whole = {0x03, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
	SIM_EXPECT(layer.Take(VUB::Meta(), std::span<const uint8>(whole.data(), whole.size())).IsErr() == false);
	SIM_EXPECT(finalCount == 1);
	SIM_EXPECT(lastFinalResult == ResultStatus::ok);
}


// The interim index is one byte: 255 interims of one request go out with indexes 1 to 255, and the 256th is refused as invalidParameter
// without a packet. Each interim is sent in its own Process, as the layer sends one pending interim at a time.
void TestInterimIndexLimit() {
	ResetWireState();
	Layer layer;
	Recorder recorder;
	std::array<uint8, 10> start = {0x01, 0x12, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x2C, 0x01};
	SIM_EXPECT(layer.Take(VUB::Meta(), std::span<const uint8>(start.data(), start.size())).IsErr() == false);
	layer.Process(1000, OutboxOf(recorder));
	SIM_EXPECT(startHeld);
	for (uint32 index = 1; index <= 255; index++) {
		SIM_EXPECT(heldStart.Interim(Motor::MotorStatus::warming) == ResultStatus::ok);
		layer.Process(1000 + index, OutboxOf(recorder));
	}

	SIM_EXPECT(recorder.count == 255);
	std::array<uint8, 6> last = {0x02, 0x01, 0x00, 0x01, 0xFF, 0x01};
	SIM_EXPECT(recorder.last.length == last.size());
	SIM_EXPECT(recorder.last.bytes[0] == last[0]);
	SIM_EXPECT(recorder.last.bytes[4] == last[4]);
	SIM_EXPECT(recorder.last.bytes[5] == last[5]);
	SIM_EXPECT(heldStart.Interim(Motor::MotorStatus::warming) == ResultStatus::invalidParameter);
	layer.Process(2000, OutboxOf(recorder));
	SIM_EXPECT(recorder.count == 255);
}


// Takes a recorded packet into a layer, as its other end would: true when the layer accepts it.
template<typename LayerType>
bool TakeSent(LayerType& target, const Sent& packet) {
	return target.Take(VUB::Meta(), std::span<const uint8>(packet.bytes.data(), packet.length)).IsErr() == false;
}


// A layer with no Reliable below sends nothing while no session is given: Send returns noInit and no packet goes out. Session 0 is given
// here, and the first request is number 1 with tag 0, because number 0 is never sent.
void TestNoSessionIsNoInit() {
	ResetWireState();
	SessionLayer layer;
	Recorder recorder;
	SessionLayer::Handle handle(layer, OutboxOf(recorder));
	Motor::MotorStart start{};
	auto refused = handle.template Send<Motor::MotorCommand::start, &OnStartFinal, &OnStartInterim>(start);
	SIM_EXPECT(refused.IsErr());
	SIM_EXPECT(refused.Error() == ResultStatus::noInit);
	SIM_EXPECT(recorder.count == 0);
	handle.SetSession(0);
	SIM_EXPECT(SendStart(handle, start));
	SIM_EXPECT(recorder.count == 1);
	std::array<uint8, 10> firstStart = {0x01, 0x12, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x2C, 0x01};
	SIM_EXPECT(SentIs(recorder, 0, firstStart));
}


// Two boots with different sessions. The first boot's request is still open on the responder. The second boot's first request has another
// number and tag (session 0x0102 gives number 0x736E, the low 16 bits of 0x0102 x 0x9E37, and tag 0x02), so the responder admits it and
// sends nothing.
void TestSecondBootIsNotDuplicate() {
	ResetWireState();
	SessionLayer firstBoot;
	Recorder firstRecorder;
	SessionLayer::Handle first(firstBoot, OutboxOf(firstRecorder));
	first.SetSession(1);
	Motor::MotorStart start{};
	SIM_EXPECT(SendStart(first, start));
	SIM_EXPECT(firstRecorder.count == 1);
	SessionLayer responder;
	Recorder answers;
	SIM_EXPECT(TakeSent(responder, firstRecorder.sent[0]));
	responder.Process(500, OutboxOf(answers));
	SIM_EXPECT(seenStartCount == 1);

	SessionLayer secondBoot;
	Recorder secondRecorder;
	SessionLayer::Handle second(secondBoot, OutboxOf(secondRecorder));
	second.SetSession(0x0102);
	SIM_EXPECT(SendStart(second, start));
	SIM_EXPECT(secondRecorder.count == 1);
	SIM_EXPECT(TakeSent(responder, secondRecorder.sent[0]));
	responder.Process(1000, OutboxOf(answers));
	SIM_EXPECT(seenStartCount == 2);
	SIM_EXPECT(answers.count == 0);
	SIM_EXPECT(heldStart.Open());
	SIM_EXPECT(heldStart.Number().number == 0x736E);
}


// Two boots whose sessions follow each other, as a stored counter gives them. The second request of the first boot is open on the
// responder. The first request of the second boot is another request: the responder admits it, and the final of the old one does not close it.
void TestNeighbouringSessionsAreNotDuplicates() {
	ResetWireState();
	SessionLayer firstBoot;
	Recorder firstRecorder;
	SessionLayer::Handle first(firstBoot, OutboxOf(firstRecorder));
	first.SetSession(1);
	Motor::MotorStart start{};
	SIM_EXPECT(SendStart(first, start));
	SIM_EXPECT(SendStart(first, start));
	SIM_EXPECT(firstRecorder.count == 2);
	SessionLayer responder;
	Recorder answers;
	SIM_EXPECT(TakeSent(responder, firstRecorder.sent[1]));
	responder.Process(500, OutboxOf(answers));
	SIM_EXPECT(seenStartCount == 1);
	StartTicket oldTicket = heldStart;

	SessionLayer secondBoot;
	Recorder secondRecorder;
	SessionLayer::Handle second(secondBoot, OutboxOf(secondRecorder));
	second.SetSession(2);
	SIM_EXPECT(SendStart(second, start));
	SIM_EXPECT(TakeSent(responder, secondRecorder.sent[0]));
	responder.Process(1000, OutboxOf(answers));
	SIM_EXPECT(seenStartCount == 2);
	SIM_EXPECT(answers.count == 0);

	Motor::MotorReport report{};
	SIM_EXPECT(oldTicket.Finish(Motor::MotorStatus::started, report) == ResultStatus::ok);
	responder.Process(1500, OutboxOf(answers));
	SIM_EXPECT(answers.count == 1);
	std::size_t finalsBefore = finalCount;
	(void)secondBoot.Take(VUB::Meta(), std::span<const uint8>(answers.last.bytes.data(), answers.last.length));
	SIM_EXPECT(finalCount == finalsBefore);
}


// A status of 0xF0 or more is refused by Interim and by Finish with invalidParameter, and sends nothing: the ticket stays open. A status of
// 0xEF is an ordinary status, and its final goes out.
void TestReservedStatusRefused() {
	ResetWireState();
	Layer layer;
	Recorder recorder;
	std::array<uint8, 8> start = {0x01, 0x12, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00};
	SIM_EXPECT(layer.Take(VUB::Meta(), std::span<const uint8>(start.data(), start.size())).IsErr() == false);
	layer.Process(500, OutboxOf(recorder));
	SIM_EXPECT(startHeld);
	Motor::MotorReport report{};
	auto reservedLow = static_cast<Motor::MotorStatus>(0xF0);
	auto reservedHigh = static_cast<Motor::MotorStatus>(0xFF);
	SIM_EXPECT(heldStart.Interim(reservedLow) == ResultStatus::invalidParameter);
	SIM_EXPECT(heldStart.Interim(reservedHigh) == ResultStatus::invalidParameter);
	SIM_EXPECT(heldStart.Finish(reservedLow, report) == ResultStatus::invalidParameter);
	SIM_EXPECT(heldStart.Open());
	layer.Process(1000, OutboxOf(recorder));
	SIM_EXPECT(recorder.count == 0);
	SIM_EXPECT(heldStart.Finish(static_cast<Motor::MotorStatus>(0xEF), report) == ResultStatus::ok);
	layer.Process(2000, OutboxOf(recorder));
	SIM_EXPECT(recorder.count == 1);
	std::array<uint8, 10> finalLast = {0x03, 0x01, 0x00, 0x01, 0x00, 0xEF, 0x00, 0x00, 0x00, 0x00};
	SIM_EXPECT(SentIs(recorder, 0, finalLast));
}


int main() {
	TestRequestBytes();
	TestAnswerBytes();
	TestResponderBytes();
	TestRefusalBytes();
	TestOpenNumberBytes();
	TestFireBytes();
	TestFireWithoutTickets();
	TestMalformed();
	TestFinalWithoutStatus();
	TestInterimIndexLimit();
	TestNoSessionIsNoInit();
	TestSecondBootIsNotDuplicate();
	TestNeighbouringSessionsAreNotDuplicates();
	TestReservedStatusRefused();
	return SimFinish("TestVubRequestsWire");
}
