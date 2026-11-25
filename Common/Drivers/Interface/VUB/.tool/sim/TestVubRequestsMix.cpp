#include "RequestsMotor.h"
#include "SimCheck.h"
#include <Drivers/Interface/VUB/Outbox.h>
#include <Drivers/Interface/VUB/Requests.h>
#include <Utilities/DataTypes.h>
#include <array>
#include <cstddef>
#include <span>

// The features of the requests layer together: a cancel, an event, an interim and a ping share the slots and the packets of one layer. The
// layer is driven without a stack: a recording outbox takes what the layer sends, and hand-written packets go in through Take. There is no
// Reliable below, so a CANCEL goes out at once and the numbers start at the session's (session 30599 gives number 1 and tag 0x87).


inline std::size_t startFinals = 0;
inline ResultStatus startResult = ResultStatus::ok;
inline std::size_t interims = 0;
inline std::size_t pings = 0;
inline ResultStatus pingResult = ResultStatus::ok;
inline uint32 pingTripUs = 0;
inline std::size_t faults = 0;
inline uint8 faultCode = 0;


void OnStartFinal(const VUB::Final<Motor::MotorStatus, Motor::MotorReport>& reply) {
	startFinals++;
	startResult = reply.result;
}


void OnStartInterim(const VUB::Progress<Motor::MotorStatus>&) {
	interims++;
}


void OnPing(ResultStatus result, uint32 roundTripUs) {
	pings++;
	pingResult = result;
	pingTripUs = roundTripUs;
}


void OnFault(const Motor::MotorFault& fault) {
	faults++;
	faultCode = fault.code;
}


using MixHandlers = VUB::Handlers<VUB::OnEvent<Motor::MotorEvent::fault, &OnFault>>;


// One slot, so that each request and each ping takes the slot of the one before it. Counted, with pings of up to 4 bytes of padding and a
// deadline of 100 ms.
using Layer = VUB::RequestsOf<
	Motor::MotorCommands,
	MixHandlers,
	VUB::RequestsOptions{.open = 1, .refusals = 0, .counted = true, .ping = true, .pingPadding = 4, .builtInTimeoutMs = 100}>;


// Two slots, for a ping next to a request.
using WideLayer = VUB::RequestsOf<
	Motor::MotorCommands,
	MixHandlers,
	VUB::RequestsOptions{.open = 2, .refusals = 0, .counted = true, .ping = true, .pingPadding = 4, .builtInTimeoutMs = 100}>;


struct Recorder {
	std::array<std::array<uint8, 32>, 16> packets{};
	std::array<std::size_t, 16> lengths{};
	std::size_t count = 0;
	ResultStatus answer = ResultStatus::ok;
};


ResultStatus RecordSend(void* owner, std::span<const uint8> packet, const VUB::Meta&) {
	Recorder* recorder = static_cast<Recorder*>(owner);
	if (recorder->answer != ResultStatus::ok) {
		return recorder->answer;
	}

	if (recorder->count < recorder->packets.size()) {
		for (std::size_t index = 0; index < packet.size() && index < recorder->packets[recorder->count].size(); index++) {
			recorder->packets[recorder->count][index] = packet[index];
		}

		recorder->lengths[recorder->count] = packet.size();
		recorder->count++;
	}

	return ResultStatus::ok;
}


VUB::Outbox OutboxOf(Recorder& recorder) {
	return VUB::Outbox(&recorder, &RecordSend, nullptr, nullptr);
}


// The packets of a kind that were sent.
std::size_t CountKind(const Recorder& recorder, uint8 kind) {
	std::size_t found = 0;
	for (std::size_t index = 0; index < recorder.count; index++) {
		if (recorder.packets[index][0] == kind) {
			found++;
		}
	}

	return found;
}


template<std::size_t Size>
bool SentIs(const Recorder& recorder, std::size_t index, const std::array<uint8, Size>& expected) {
	if (index >= recorder.count || recorder.lengths[index] != Size) {
		return false;
	}

	for (std::size_t byte = 0; byte < Size; byte++) {
		if (recorder.packets[index][byte] != expected[byte]) {
			return false;
		}
	}

	return true;
}


template<typename LayerType, std::size_t Size>
bool Takes(LayerType& layer, const std::array<uint8, Size>& packet) {
	return layer.Take(VUB::Meta(), std::span<const uint8>(packet.data(), packet.size())).IsErr() == false;
}


void ResetMix() {
	startFinals = 0;
	startResult = ResultStatus::ok;
	interims = 0;
	pings = 0;
	pingResult = ResultStatus::ok;
	pingTripUs = 0;
	faults = 0;
	faultCode = 0;
}


template<typename HandleType>
VUB::RequestId SendStart(HandleType& handle) {
	Motor::MotorStart body{};
	Result<VUB::RequestId> sent = handle.template Send<Motor::MotorCommand::start, &OnStartFinal, &OnStartInterim>(body);
	SIM_EXPECT(!sent.IsErr());
	return sent.IsErr() ? VUB::RequestId{} : sent.Value();
}


// A cancel that the stack refused is held in its slot. Its request ends with the final, and a ping takes the slot: the ping sends no CANCEL,
// and at its deadline it ends with timeout.
void TestPingAfterHeldCancel() {
	ResetMix();
	Layer layer;
	Recorder recorder;
	Layer::Handle handle(layer, OutboxOf(recorder));
	handle.SetSession(30599);
	VUB::RequestId id = SendStart(handle);
	SIM_EXPECT(id.number == 1);
	recorder.answer = ResultStatus::busy;
	SIM_EXPECT(handle.Cancel<Motor::MotorCommand::start>(id) == ResultStatus::ok);
	recorder.answer = ResultStatus::ok;
	std::array<uint8, 10> final = {0x03, 0x01, 0x00, 0x87, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
	SIM_EXPECT(Takes(layer, final));
	SIM_EXPECT(startFinals == 1);
	SIM_EXPECT(startResult == ResultStatus::ok);

	SIM_EXPECT(handle.Ping<&OnPing>() == ResultStatus::ok);
	layer.Process(1000, OutboxOf(recorder));
	layer.Process(2000, OutboxOf(recorder));
	std::array<uint8, 9> ping = {0x01, 0xF0, 0x02, 0x00, 0x88, 0xE8, 0x03, 0x00, 0x00};
	SIM_EXPECT(recorder.count == 2);
	SIM_EXPECT(SentIs(recorder, 1, ping));
	SIM_EXPECT(CountKind(recorder, 0x05) == 0);
	layer.Process(101000, OutboxOf(recorder));
	SIM_EXPECT(pings == 1);
	SIM_EXPECT(pingResult == ResultStatus::timeout);
	SIM_EXPECT(CountKind(recorder, 0x05) == 0);
}


// CancelAll with a ping open next to a request: the request gets its CANCEL, the ping gets none, and the ping's reply still ends it.
void TestCancelAllWithPing() {
	ResetMix();
	WideLayer layer;
	Recorder recorder;
	WideLayer::Handle handle(layer, OutboxOf(recorder));
	handle.SetSession(30599);
	SIM_EXPECT(handle.Ping<&OnPing>() == ResultStatus::ok);
	SIM_EXPECT(handle.CancelAll() == ResultStatus::ok);
	SIM_EXPECT(recorder.count == 0);
	layer.Process(1000, OutboxOf(recorder));
	SIM_EXPECT(recorder.count == 1);
	(void)SendStart(handle);
	SIM_EXPECT(handle.CancelAll() == ResultStatus::ok);
	std::array<uint8, 4> cancel = {0x05, 0x02, 0x00, 0x88};
	SIM_EXPECT(recorder.count == 3);
	SIM_EXPECT(SentIs(recorder, 2, cancel));
	std::array<uint8, 10> reply = {0x03, 0x01, 0x00, 0x87, 0x00, 0x00, 0xE8, 0x03, 0x00, 0x00};
	SIM_EXPECT(Takes(layer, reply));
	layer.Process(3500, OutboxOf(recorder));
	SIM_EXPECT(pings == 1);
	SIM_EXPECT(pingResult == ResultStatus::ok);
	SIM_EXPECT(pingTripUs == 2500);
	SIM_EXPECT(startFinals == 0);
	SIM_EXPECT(CountKind(recorder, 0x05) == 1);
}


// A request that follows a ping in the same slot has no state of it: its interims arrive from index 1, and a cancel goes out once.
void TestRequestAfterPing() {
	ResetMix();
	Layer layer;
	Recorder recorder;
	Layer::Handle handle(layer, OutboxOf(recorder));
	handle.SetSession(30599);
	SIM_EXPECT(handle.Ping<&OnPing>(4) == ResultStatus::ok);
	layer.Process(1000, OutboxOf(recorder));
	std::array<uint8, 14> reply = {0x03, 0x01, 0x00, 0x87, 0x00, 0x00, 0xE8, 0x03, 0x00, 0x00, 0x00, 0x01, 0x02, 0x03};
	SIM_EXPECT(Takes(layer, reply));
	layer.Process(1400, OutboxOf(recorder));
	SIM_EXPECT(pings == 1);
	SIM_EXPECT(pingTripUs == 400);

	VUB::RequestId id = SendStart(handle);
	SIM_EXPECT(id.number == 2);
	std::array<uint8, 6> interim = {0x02, 0x02, 0x00, 0x88, 0x01, 0x01};
	SIM_EXPECT(Takes(layer, interim));
	SIM_EXPECT(interims == 1);
	SIM_EXPECT(handle.Cancel<Motor::MotorCommand::start>(id) == ResultStatus::ok);
	SIM_EXPECT(handle.Cancel<Motor::MotorCommand::start>(id) == ResultStatus::ok);
	SIM_EXPECT(CountKind(recorder, 0x05) == 1);
	std::array<uint8, 5> cancelled = {0x03, 0x02, 0x00, 0x88, 0x01};
	SIM_EXPECT(Takes(layer, cancelled));
	SIM_EXPECT(startFinals == 1);
	SIM_EXPECT(startResult == ResultStatus::operationCancelled);
}


// An interim and a FINAL that name a ping's number: the interim changes nothing, and a FINAL of a refusal ends the ping with its result. An
// event in between reaches its handler.
void TestInterimAndEventWhilePing() {
	ResetMix();
	Layer layer;
	Recorder recorder;
	Layer::Handle handle(layer, OutboxOf(recorder));
	handle.SetSession(30599);
	SIM_EXPECT(handle.Ping<&OnPing>() == ResultStatus::ok);
	layer.Process(1000, OutboxOf(recorder));
	std::array<uint8, 6> interim = {0x02, 0x01, 0x00, 0x87, 0x01, 0x01};
	SIM_EXPECT(Takes(layer, interim));
	std::array<uint8, 6> refusedCancel = {0x02, 0x01, 0x00, 0x87, 0x02, 0xF8};
	SIM_EXPECT(Takes(layer, refusedCancel));
	SIM_EXPECT(interims == 0);
	std::array<uint8, 3> event = {0x04, static_cast<uint8>(Motor::MotorEvent::fault), 0x2A};
	SIM_EXPECT(Takes(layer, event));
	SIM_EXPECT(faults == 1);
	SIM_EXPECT(faultCode == 0x2A);
	std::array<uint8, 5> refused = {0x03, 0x01, 0x00, 0x87, 0x02};
	SIM_EXPECT(Takes(layer, refused));
	layer.Process(2000, OutboxOf(recorder));
	SIM_EXPECT(pings == 1);
	SIM_EXPECT(pingResult == ResultStatus::notSupported);
	SIM_EXPECT(pingTripUs == 0);
}


// The late answers of a request and of a ping are counted, and so is an event the side has no handler for.
void TestCountersOfAllKinds() {
	ResetMix();
	Layer layer;
	Recorder recorder;
	Layer::Handle handle(layer, OutboxOf(recorder));
	handle.SetSession(30599);
	std::array<uint8, 10> lateFinal = {0x03, 0x01, 0x00, 0x87, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
	SIM_EXPECT(Takes(layer, lateFinal));
	std::array<uint8, 6> lateInterim = {0x02, 0x01, 0x00, 0x87, 0x01, 0x01};
	SIM_EXPECT(Takes(layer, lateInterim));
	SIM_EXPECT(handle.LateReplies() == 2);
	std::array<uint8, 3> unknownEvent = {0x04, 0x7E, 0x00};
	SIM_EXPECT(Takes(layer, unknownEvent));
	SIM_EXPECT(handle.Refused() == 1);
	SIM_EXPECT(handle.EventsLost() == 0);
	SIM_EXPECT(faults == 0);
}


int main() {
	TestPingAfterHeldCancel();
	TestCancelAllWithPing();
	TestRequestAfterPing();
	TestInterimAndEventWhilePing();
	TestCountersOfAllKinds();
	return SimFinish("TestVubRequestsMix");
}
