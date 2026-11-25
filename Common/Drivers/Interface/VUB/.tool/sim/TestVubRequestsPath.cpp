#include "RequestsChains.h"
#include <Drivers/Interface/VUB/Outbox.h>
#include <Drivers/Interface/VUB/Requests.h>
#include <Utilities/DataTypes.h>
#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <utility>

// The paths of a request through the layers: the chains (a handler that sends the next request, a chain with no delivery layer below),
// the links (a loss, a link down, a request kept or not kept over a reconnect), and the restarts of the peer. The stacks and the commands are
// in RequestsChains.h.


// The link that takes what the line delivers and passes none of it on.
struct LostSink {
	void Feed(uint8) {
	}


	void FeedPacket(std::span<const uint8>) {
	}
};


// The final handler of a request sends the next request from inside the handler, with one slot: the slot is freed before the handler runs.
using ChainLayer = VUB::RequestsOf<RequesterCommands, VUB::Handlers<>, VUB::RequestsOptions{1, 0, 0, true}>;


inline ChainLayer::Handle* chainHandle = nullptr;
inline std::size_t chainFinals = 0;
inline ResultStatus chainResult = ResultStatus::notAvailable;
inline uint16 chainNext = 0;


void OnChainFinal(const VUB::Final<Motor::MotorStatus, VUB::NoBody>& reply) {
	chainFinals++;
	chainResult = reply.result;
	if (chainFinals == 1 && chainHandle != nullptr) {
		auto next = chainHandle->template Send<RequesterCommand::stop,&OnChainFinal>(VUB::NoBody{});
		if (next.IsErr() == false) {
			chainNext = next.Value().number;
		}
	}
}


// The interim handler of a request sends another request: the interim is taken, the handler sends, and the new request takes the next number.
inline ChainLayerTwo::Handle* chainHandleTwo = nullptr;
inline std::size_t chainInterims = 0;
inline uint16 chainInterimNext = 0;


void OnChainInterim(const VUB::Progress<Motor::MotorStatus>&) {
	chainInterims++;
	if (chainInterims == 1 && chainHandleTwo != nullptr) {
		auto next = chainHandleTwo->template Send<RequesterCommand::stop,&OnChainStopFinal>(VUB::NoBody{});
		if (next.IsErr() == false) {
			chainInterimNext = next.Value().number;
		}
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
		auto next = inOrderHandle->template Send<RequesterCommand::stop,&OnInOrderFinal>(VUB::NoBody{});
		if (next.IsErr() == false) {
			inOrderNext = next.Value().number;
		}
	}

	(void)reply;
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
	// The requester has nothing in flight after the acknowledgement, so the second boot shows only when it sends again: a second request is
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


// The link goes down: a FailWhileDown request is closed with noConnection, and a further one of it is refused at once; a request of a command
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
	auto refused = handle.Send<RequesterCommand::stop,&OnStopFinalR>(VUB::NoBody{});
	SIM_EXPECT(refused.IsErr());
	SIM_EXPECT(refused.Error() == ResultStatus::noConnection);

	// motor.start waits: the send is accepted while down, and its deadline ends it.
	auto waiting = handle.Send<RequesterCommand::start,&OnStartFinalR>(Motor::MotorStart{});
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


// The final handler of a request sends the next request from inside the handler, with one slot: the slot is freed first, then the handler runs.
void TestFinalHandlerSendsNextRequest() {
	chainFinals = 0;
	chainNext = 0;
	chainResult = ResultStatus::notAvailable;
	ChainLayer layer;
	Probe probe;
	ChainLayer::Handle handle(layer, ProbeOutbox(probe));
	chainHandle = &handle;
	auto first = handle.template Send<RequesterCommand::stop,&OnChainFinal>(VUB::NoBody{});
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


// The interim handler of a request sends another request: the interim is taken, the handler sends, and that request takes the next number.
void TestInterimHandlerSendsNextRequest() {
	chainInterims = 0;
	chainInterimNext = 0;
	ChainLayerTwo layer;
	Probe probe;
	ChainLayerTwo::Handle handle(layer, ProbeOutbox(probe));
	chainHandleTwo = &handle;
	auto start = handle.template Send<RequesterCommand::start,&OnChainStartFinal, &OnChainInterim>(Motor::MotorStart{});
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
	auto probe = handle.template Send<RequesterCommand::stop,&OnInOrderFinal>(VUB::NoBody{});
	SIM_EXPECT(probe.IsErr());
	SIM_EXPECT(probe.Error() == ResultStatus::busy);
	std::array<uint8, 1> trigger = {0xEE};
	SIM_EXPECT(far.LayerOutbox<0>().Send(std::span<const uint8>(trigger.data(), trigger.size())) == ResultStatus::busy);
	for (uint32 round = 0; round < 3; round++) {
		RoundOf(line, near, far, log, 100 + round);
	}

	inOrderHandle = &handle;
	auto first = handle.template Send<RequesterCommand::stop,&OnInOrderFinal>(VUB::NoBody{});
	SIM_EXPECT(first.IsErr() == false);
	for (uint32 round = 0; round < 6; round++) {
		RoundOf(line, near, far, log, 1000 + round * 10);
	}

	SIM_EXPECT(inOrderFinals == 2);
	SIM_EXPECT(inOrderNext == 2);
	SIM_EXPECT(stopsSeen == 2);
	inOrderHandle = nullptr;
}


void TestPlainReplyAtOnce() {
	ResetRequestsState();
	stopAtOnce = true;
	ReportingPacketLine line(VUB::Faults{});
	PlainRequester near(line.a);
	PlainResponder far(line.b);
	WireLog log;
	RoundOf(line, near, far, log, 50);
	auto handle = near.Requests();
	handle.SetSession(1);
	SIM_EXPECT(SendStopR(handle));
	RoundOf(line, near, far, log, 1000);
	SIM_EXPECT(stopsSeen == 1);
	SIM_EXPECT(stopFinals == 1);
	SIM_EXPECT(stopResult == ResultStatus::ok);
	// Session 1: the first number is 0x9E37, and the first tag is 1.
	std::array<uint8, 5> request = {0x01, 0x13, 0x37, 0x9E, 0x01};
	SIM_EXPECT(log.toFar.count == 1);
	SIM_EXPECT(CaptureIs(log.toFar, 0, request));
	std::array<uint8, 6> finalStarted = {0x03, 0x37, 0x9E, 0x01, 0x00, 0x00};
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
	handle.SetSession(1);
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
	handle.SetSession(1);
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
	handle.SetSession(1);
	auto unknown =handle.template Send<RequesterCommand::unknown,&OnUnknownFinalR>(VUB::NoBody{});
	SIM_EXPECT(unknown.IsErr() == false);
	RoundOf(line, near, far, log, 1000);
	SIM_EXPECT(unknownFinals == 1);
	SIM_EXPECT(unknownResult == ResultStatus::notSupported);
}


void TestPlainFire() {
	ResetServeState();
	ReportingPacketLine line(VUB::Faults{});
	PlainServeRequester near(line.a);
	PlainServeResponder far(line.b);
	WireLog log;
	auto handle = near.Requests();
	SIM_EXPECT(handle.template Fire<ServeCommand::kick>(Motor::MotorFault{7}) == ResultStatus::ok);
	RoundOf(line, near, far, log, 1000);
	SIM_EXPECT(serveKickCount == 1);
	SIM_EXPECT(serveKickCode == 7);
	std::array<uint8, 6> kick = {0x01, 0x15, 0x01, 0x00, 0x01, 0x07};
	SIM_EXPECT(log.toFar.count == 1);
	SIM_EXPECT(CaptureIs(log.toFar, 0, kick));
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


// A request sent while the link is down, and its deadline passed while it is down: its packet was never on the wire. When the link returns it is
// not sent, and the responder's handler does not run.
void TestRequestSentWhileDownNotRunAfterTimeout() {
	ResetRequestsState();
	ReportingPacketLine line(VUB::Faults{});
	Requester near(line.a);
	Responder far(line.b);
	WireLog log;
	Establish(line, near, far, log);
	auto handle = near.Requests();
	line.SetLinkUp(false);
	testNowUs = 1000;
	line.SetNow(1000);
	SIM_EXPECT(SendStartR(handle));
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


int main() {
	TestPeerRestartClosesOpenRequests();
	TestLinkDown();
	TestFinalHandlerSendsNextRequest();
	TestInterimHandlerSendsNextRequest();
	TestFinalHandlerSendsNextRequestInOrder();
	TestPlainReplyAtOnce();
	TestPlainInterimAndFinal();
	TestPlainTimeout();
	TestPlainRefusal();
	TestPlainFire();
	TestTimedOutRequestNotResent();
	TestFailWhileDownRequestNotResent();
	TestOpenRequestResentOnUp();
	TestRequestSentWhileDownNotRunAfterTimeout();
	return SimFinish("TestVubRequestsPath");
}
