#include "Chains.h"
#include "ReliableChains.h"
#include "RequestsMotor.h"
#include "SimCheck.h"
#include <Drivers/Interface/VUB/Framing/Cobs.h>
#include <Drivers/Interface/VUB/Layer/KeepAlive.h>
#include <Drivers/Interface/VUB/LinkTraits.h>
#include <Drivers/Interface/VUB/Meta.h>
#include <Drivers/Interface/VUB/Outbox.h>
#include <Drivers/Interface/VUB/Requests.h>
#include <Drivers/Interface/VUB/Stack.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/Result.h>
#include <Utilities/Result/ResultStatus.h>
#include <array>
#include <cstddef>
#include <cstdio>
#include <span>

// Starvation: a layer's packet that the link refuses as busy must go before the next packet of the application. The application sends
// after every Process until Send returns busy, over a link that takes part of a frame per Process (a byte stream) or that is free only
// between two Process calls (a packet link). Each test counts what the layers and the application sent, so a test shows the share of both.
//
// Clocks: the chains use testNowUs of Chains.h. A step moves it by 100 us.


// The bytes one end wrote, in order, for the peer to read.
struct StarveBytes {
	std::array<uint8, 65536> data{};
	std::size_t head = 0;
	std::size_t tail = 0;
};


// A byte-stream link that takes up to the budget of the round, and writes what it takes into its outgoing bytes.
class StarveStream {
public:
	static constexpr VUB::LinkTraits traits = VUB::LinkTraits().MaxPacket(64).HalfDuplex();


	explicit StarveStream(StarveBytes& outgoing) : out(&outgoing) {
	}


	void StartRound(std::size_t bytes) {
		budget = bytes;
	}


	Result<std::size_t> Write(std::span<const uint8> bytes) {
		std::size_t accepted = bytes.size() < budget ? bytes.size() : budget;
		for (std::size_t index = 0; index < accepted; index++) {
			if (out->tail < out->data.size()) {
				out->data[out->tail] = bytes[index];
			}

			out->tail++;
		}

		budget -= accepted;
		return accepted;
	}


private:
	StarveBytes* out;
	std::size_t budget = 0;
};


// Feeds the bytes a peer wrote into the peer's stack, and forgets them.
template<typename StackType>
void StarveDeliver(StarveBytes& from, StackType& peer) {
	while (from.head < from.tail && from.head < from.data.size()) {
		peer.Feed(from.data[from.head]);
		from.head++;
	}
}


// A packet link that is free for one Send at a time: Ready() is true while its slot is free, Send takes the packet and makes the slot busy,
// and FreeLink makes it free again. The test calls FreeLink between two Process calls, as a link does when its last packet has left. A
// packet whose ordinal is dropOrdinal is taken by the link and not delivered, as a line loses it.
class StarveReady {
public:
	static constexpr VUB::LinkTraits traits = VUB::LinkTraits().Framed().MaxPacket(64).FullDuplex();


	bool Ready() const {
		return free;
	}


	ResultStatus Send(std::span<const uint8> packet) {
		if (!free) {
			return ResultStatus::busy;
		}

		free = false;
		sent++;
		if (count < packets.size()) {
			for (std::size_t index = 0; index < packet.size() && index < 64; index++) {
				packets[count][index] = packet[index];
			}

			lengths[count] = packet.size();
			dropped[count] = sent == dropOrdinal;
			count++;
		}

		return ResultStatus::ok;
	}


	void FreeLink() {
		free = true;
	}


	// Makes the slot busy, as a packet the link is still sending does.
	void TakeSlot() {
		free = false;
	}


	// Hands the packets sent since the last call to the peer, and forgets them.
	template<typename StackType>
	void DeliverTo(StackType& peer) {
		for (std::size_t index = 0; index < count; index++) {
			if (!dropped[index]) {
				peer.FeedPacket(std::span<const uint8>(packets[index].data(), lengths[index]));
			}
		}

		count = 0;
	}


	// Copies the packets sent since the last DeliverTo into the log, as bytes.
	void CopyTo(std::array<std::array<uint8, 64>, 32>& log, std::array<std::size_t, 32>& logLengths, std::size_t& logCount) const {
		for (std::size_t index = 0; index < count; index++) {
			if (logCount < log.size()) {
				for (std::size_t byteIndex = 0; byteIndex < lengths[index]; byteIndex++) {
					log[logCount][byteIndex] = packets[index][byteIndex];
				}

				logLengths[logCount] = lengths[index];
			}

			logCount++;
		}
	}


	std::size_t sent = 0;
	std::size_t dropOrdinal = 0;


private:
	std::array<std::array<uint8, 64>, 8> packets{};
	std::array<std::size_t, 8> lengths{};
	std::array<bool, 8> dropped{};
	std::size_t count = 0;
	bool free = true;
};


// A layer that has a packet of its own every eighth Process, and keeps it until the link takes it. It has no peer and no queue of its
// own: it is a layer of the control lane, like a Reliable resend. A layer that wanted the link at every Process would take every
// boundary of a frame and leave the application none; the queues share the link in order, and the direct path does not (see the report).
struct StarveSpamLayer {
	static constexpr std::size_t header = 0;
	static constexpr uint32 fingerprint = 0x53504D31;
	static constexpr bool usesTime = false;
	static constexpr bool makesBulk = false;


	Result<std::size_t> Add(const VUB::Meta&, std::span<const uint8> in, std::span<uint8> out) {
		if (out.size() < in.size()) {
			return ResultStatus::invalidParameter;
		}

		for (std::size_t index = 0; index < in.size(); index++) {
			out[index] = in[index];
		}

		return in.size();
	}


	Result<VUB::Taken> Take(const VUB::Meta&, std::span<const uint8>) {
		return VUB::Taken{0, true};
	}


	void Process(uint32, const VUB::Outbox& outbox) {
		if (!pending) {
			countdown++;
			if (countdown < 8) {
				return;
			}

			countdown = 0;
			packet[0] = 0x5A;
			packet[1] = seq;
			seq++;
			pending = true;
		}

		if (outbox.Send(std::span<const uint8>(packet.data(), packet.size())) == ResultStatus::ok) {
			pending = false;
			sent++;
		} else {
			refused++;
		}
	}


	std::array<uint8, 2> packet{};
	uint8 seq = 0;
	uint8 countdown = 0;
	bool pending = false;
	uint32 sent = 0;
	uint32 refused = 0;
};


// KeepAlive over a byte stream with COBS, and the application's payloads.
using StarveKeepAliveStream = VUB::Stack<+[] {
	return VUB::Builder()
		.CountDrops()
		.Layer<VUB::KeepAlive>().QuietTime(1000).DownTime(3000)
		.Framing<VUB::Cobs>()
		.Link<StarveStream>()
		.MaxPayload(40)
		.ReceiveRing(512)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


// KeepAlive over a byte stream with a send queue of one packet.
using StarveKeepAliveQueued = VUB::Stack<+[] {
	return VUB::Builder()
		.CountDrops()
		.Layer<VUB::KeepAlive>().QuietTime(1000).DownTime(3000)
		.Framing<VUB::Cobs>()
		.Link<StarveStream>()
		.MaxPayload(40)
		.ReceiveRing(512)
		.SendQueue(1)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


// The spam layer over a byte stream with no send queue, and with queues of one and of four packets.
using StarveSpamDirect = VUB::Stack<+[] {
	return VUB::Builder()
		.Layer<StarveSpamLayer>()
		.Framing<VUB::Cobs>()
		.Link<StarveStream>()
		.MaxPayload(40)
		.ReceiveRing(512)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


using StarveSpamQueue1 = VUB::Stack<+[] {
	return VUB::Builder()
		.Layer<StarveSpamLayer>()
		.Framing<VUB::Cobs>()
		.Link<StarveStream>()
		.MaxPayload(40)
		.ReceiveRing(512)
		.SendQueue(1)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


using StarveSpamQueue4 = VUB::Stack<+[] {
	return VUB::Builder()
		.Layer<StarveSpamLayer>()
		.Framing<VUB::Cobs>()
		.Link<StarveStream>()
		.MaxPayload(40)
		.ReceiveRing(512)
		.SendQueue(4)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


// Reliable over a packet link that is free between Process calls. Its window is 16, so the application keeps sending while a packet is lost
// and its resend waits: with a window of 4 the application stops for the lost packet, and the link is free for the resend.
using StarveReliableLayer = VUB::ReliableOf<40, 16>;


using StarveReliable = VUB::Stack<+[] {
	return VUB::Builder()
		.CountDrops()
		.Layer<StarveReliableLayer>().Retries(3).TimeoutMs(200).Window(16).PayloadBytes(40).OnOutcome<&ReliableOutcome>().OnRestart<&ReliableRestart>()
		.Link<StarveReady>()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.OnReceive<&ReliableReceive>()
		.Clock<&TestClock>();
}>;


// KeepAlive over a packet link that is free between Process calls: the peer is silent, so the layer declares it down.
using StarveKeepAliveWait = VUB::Stack<+[] {
	return VUB::Builder()
		.CountDrops()
		.Layer<VUB::KeepAlive>().QuietTime(1000).DownTime(3000)
		.Link<StarveReady>()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


using StarveKeepAliveFail = VUB::Stack<+[] {
	return VUB::Builder()
		.CountDrops()
		.Layer<VUB::KeepAlive>().QuietTime(1000).DownTime(3000)
		.Link<StarveReady>()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.WhileDown(VUB::Down::Fail)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


// The application's payload of a stream: the same bytes every time.
inline std::array<uint8, 12> StarvePayload() {
	return std::array<uint8, 12>{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
}


// One step of two stream stacks, a and b. The link of each end takes perStep bytes in each Process. The application of a sends after its
// Process until Send returns busy. Returns how many of its packets were accepted.
template<typename StackType>
std::size_t StarveStreamStep(StackType& a, StackType& b, StarveStream& linkA, StarveStream& linkB, StarveBytes& ab, StarveBytes& ba,
	std::size_t perStep, bool appStreams) {
	testNowUs += 100;
	linkA.StartRound(perStep);
	a.Process();
	std::size_t accepted = 0;
	if (appStreams) {
		std::array<uint8, 12> payload = StarvePayload();
		for (std::size_t attempt = 0; attempt < 8; attempt++) {
			if (a.Send(std::span<const uint8>(payload.data(), payload.size())) != ResultStatus::ok) {
				break;
			}

			accepted++;
		}
	}

	StarveDeliver(ab, b);
	linkB.StartRound(perStep);
	b.Process();
	StarveDeliver(ba, a);
	return accepted;
}


// KeepAlive over a stream, with the application streaming on one end: the peer stays up for ten down times (30 ms each step of 100 us).
void TestKeepAliveStaysUpWhileStreaming() {
	ResetTestState();
	StarveBytes ab;
	StarveBytes ba;
	StarveStream linkA(ab);
	StarveStream linkB(ba);
	StarveKeepAliveStream a(linkA);
	StarveKeepAliveStream b(linkB);
	std::size_t accepted = 0;
	for (std::size_t step = 0; step < 300; step++) {
		accepted += StarveStreamStep(a, b, linkA, linkB, ab, ba, 3, true);
		SIM_EXPECT(a.LinkUp());
		SIM_EXPECT(b.LinkUp());
	}

	SIM_EXPECT(accepted >= 30);
}


// The same with a send queue of one packet: the keep-alive's own packets are queued on their lane, and the application's on the control lane.
void TestKeepAliveStaysUpQueuedWhileStreaming() {
	ResetTestState();
	StarveBytes ab;
	StarveBytes ba;
	StarveStream linkA(ab);
	StarveStream linkB(ba);
	StarveKeepAliveQueued a(linkA);
	StarveKeepAliveQueued b(linkB);
	std::size_t accepted = 0;
	for (std::size_t step = 0; step < 300; step++) {
		accepted += StarveStreamStep(a, b, linkA, linkB, ab, ba, 3, true);
		SIM_EXPECT(a.LinkUp());
		SIM_EXPECT(b.LinkUp());
	}

	SIM_EXPECT(accepted >= 30);
}


// A layer's packet on the control lane gets the link while the application streams, with no send queue. Both get a share.
template<typename StackType>
void SpamShare(const char* name) {
	ResetTestState();
	StarveBytes ab;
	StarveBytes ba;
	StarveStream linkA(ab);
	StarveStream linkB(ba);
	StackType a(linkA);
	StackType b(linkB);
	std::size_t accepted = 0;
	for (std::size_t step = 0; step < 300; step++) {
		accepted += StarveStreamStep(a, b, linkA, linkB, ab, ba, 3, true);
	}

	uint32 spamSent = a.template Find<StarveSpamLayer>().sent;
	std::printf("%s: layer sent %u refused %u, application accepted %zu\n", name, static_cast<unsigned>(spamSent),
		static_cast<unsigned>(a.template Find<StarveSpamLayer>().refused), accepted);
	SIM_EXPECT(spamSent >= 20);
	SIM_EXPECT(accepted >= 20);
}


void TestSpamLayerGetsLinkWhileStreamingDirect() {
	SpamShare<StarveSpamDirect>("direct");
}


void TestSpamLayerGetsLinkWhileStreamingQueue1() {
	SpamShare<StarveSpamQueue1>("queue 1");
}


void TestSpamLayerGetsLinkWhileStreamingQueue4() {
	SpamShare<StarveSpamQueue4>("queue 4");
}


// Reliable over a packet link: the first packet the link takes is lost. The application streams data while the layer resends the lost
// packet, which must go before the application's next packet. Every packet that the application got accepted is delivered once and
// settled with an ok outcome, and some packet was sent more than once.
void TestReliableResendWhileStreaming() {
	ResetReliableState();
	StarveReady linkA;
	StarveReady linkB;
	linkA.dropOrdinal = 3;
	StarveReliable a(linkA);
	StarveReliable b(linkB);
	a.template Find<StarveReliableLayer>().SetSession(ReliableSession);
	b.template Find<StarveReliableLayer>().SetSession(ReliableSession);
	std::array<uint16, 512> accepted{};
	std::size_t acceptedCount = 0;
	uint16 nextId = 1;
	for (std::size_t step = 0; step < 3000; step++) {
		testNowUs += 100;
		a.Process();
		linkA.FreeLink();
		for (std::size_t attempt = 0; attempt < 8; attempt++) {
			// The helper keeps its outcomes for ReliableIds ids; the application stops before the last of them.
			if (static_cast<std::size_t>(nextId) + 1 >= ReliableIds || ReliableSend(a, nextId) != ResultStatus::ok) {
				break;
			}

			if (acceptedCount < accepted.size()) {
				accepted[acceptedCount] = nextId;
			}

			acceptedCount++;
			nextId++;
		}

		linkA.DeliverTo(b);
		linkB.FreeLink();
		b.Process();
		linkB.DeliverTo(a);
	}

	SIM_EXPECT(acceptedCount >= 20);
	bool someResent = false;
	for (std::size_t index = 0; index < acceptedCount && index < accepted.size(); index++) {
		uint16 id = accepted[index];
		SIM_EXPECT(testOutcomes[id] == 1);
		SIM_EXPECT(testStatus[id] == ResultStatus::ok);
		SIM_EXPECT(testDelivered[id] == 1);
		if (testAttempts[id] >= 2) {
			someResent = true;
		}
	}

	SIM_EXPECT(someResent);
}


// The packets a stack sent, kept as bytes.
struct StarveLog {
	std::array<std::array<uint8, 64>, 32> packets{};
	std::array<std::size_t, 32> lengths{};
	std::size_t count = 0;
};


// Reliable over a packet link that is free between Process calls: a Send while the link is busy returns busy and takes nothing, so the layer
// keeps its window and its sequence. The packets the layer sends, with and without the refused Send before them, are the same bytes.
std::size_t StarveExchange(bool busyFirst, StarveLog* log) {
	ResetReliableState();
	StarveReady linkA;
	StarveReady linkB;
	StarveReliable a(linkA);
	StarveReliable b(linkB);
	a.template Find<StarveReliableLayer>().SetSession(ReliableSession);
	b.template Find<StarveReliableLayer>().SetSession(ReliableSession);
	// A busy send that the link refuses takes nothing. In the busy run the first send meets a busy link; then both runs send the same way,
	// once per round, until the layer accepts it (its first send starts the session, and it waits for the acknowledgement of the sync).
	if (busyFirst) {
		linkA.TakeSlot();
		SIM_EXPECT(ReliableSend(a, 1) == ResultStatus::busy);
	}

	bool sent = false;
	for (std::size_t round = 0; round < 20; round++) {
		testNowUs += 100;
		linkA.FreeLink();
		if (!sent && ReliableSend(a, 1) == ResultStatus::ok) {
			sent = true;
		}

		a.Process();
		linkA.CopyTo(log->packets, log->lengths, log->count);
		linkA.DeliverTo(b);
		linkB.FreeLink();
		b.Process();
		linkB.DeliverTo(a);
	}

	return log->count;
}


void TestBusySendTakesNothingFromReliable() {
	StarveLog reference;
	StarveLog refused;
	std::size_t referenceCount = StarveExchange(false, &reference);
	std::size_t refusedCount = StarveExchange(true, &refused);
	SIM_EXPECT(referenceCount >= 2);
	SIM_EXPECT(refusedCount == referenceCount);
	for (std::size_t index = 0; index < referenceCount && index < refusedCount && index < 32; index++) {
		SIM_EXPECT(reference.lengths[index] == refused.lengths[index]);
		for (std::size_t byteIndex = 0; byteIndex < reference.lengths[index]; byteIndex++) {
			SIM_EXPECT(reference.packets[index][byteIndex] == refused.packets[index][byteIndex]);
		}
	}
}


// KeepAlive over a packet link with no peer: the peer is declared down after the down time, the layer's ping goes while the peer is down,
// and the application's Send is busy under Wait and noConnection under Fail.
template<typename StackType>
void PeerDownPing(ResultStatus expected, const char* name) {
	ResetTestState();
	StarveReady link;
	StackType stack(link);
	std::size_t sentWhenDown = 0;
	bool declared = false;
	for (std::size_t step = 0; step < 60; step++) {
		testNowUs += 100;
		link.FreeLink();
		stack.Process();
		if (!declared && !stack.LinkUp()) {
			declared = true;
			sentWhenDown = link.sent;
		}
	}

	SIM_EXPECT(declared);
	SIM_EXPECT(!stack.LinkUp());
	SIM_EXPECT(link.sent > sentWhenDown);
	const std::array<uint8, 2> payload = {1, 2};
	ResultStatus status = stack.Send(std::span<const uint8>(payload.data(), payload.size()));
	std::printf("%s: application Send while the peer is down: %d\n", name, static_cast<int>(status));
	SIM_EXPECT(status == expected);
}


void TestKeepAlivePingGoesWhilePeerDownWait() {
	PeerDownPing<StarveKeepAliveWait>(ResultStatus::busy, "wait");
}


void TestKeepAlivePingGoesWhilePeerDownFail() {
	PeerDownPing<StarveKeepAliveFail>(ResultStatus::noConnection, "fail");
}


// Requests over Reliable over a byte stream with COBS. The start is answered with a FINAL; the kick is a fire with no reply. Both ends
// serve both commands, so either end can stream fires.
enum class StarveCall : uint8 {
	start = 0x12,
	kick = 0x15
};


using StarveCalls = VUB::CommandSet<+[]() {
	return VUB::Commands()
		.Command<StarveCall::start>()
			.Request<Motor::MotorStart>()
			.Reply<Motor::MotorStatus, Motor::MotorReport>()
			.Interim()
			.ReplyTimeoutMs(5000)
			.TicketTimeoutMs(60000)
		.Command<StarveCall::kick>()
			.Request<Motor::MotorFault>()
			.Fire();
}>;


using StarveTicket = VUB::Ticket<StarveCalls, StarveCall::start>;


inline std::size_t starveStarts = 0;
inline std::size_t starveKicks = 0;
inline std::size_t starveFinals = 0;
inline ResultStatus starveFinalResult = ResultStatus::notAvailable;
inline std::size_t starveInterims = 0;


void OnStarveStart(StarveTicket ticket, const Motor::MotorStart& body) {
	(void)body;
	starveStarts++;
	Motor::MotorReport report{};
	report.speedRpm = 1200;
	(void)ticket.Finish(Motor::MotorStatus::started, report);
}


void OnStarveKick(const Motor::MotorFault& fault) {
	(void)fault;
	starveKicks++;
}


using StarveHandlers = VUB::Handlers<
	VUB::On<StarveCall::start, &OnStarveStart>,
	VUB::On<StarveCall::kick, &OnStarveKick>>;


void OnStarveFinal(const VUB::Final<Motor::MotorStatus, Motor::MotorReport>& reply) {
	starveFinals++;
	starveFinalResult = reply.result;
}


void OnStarveInterim(const VUB::Progress<Motor::MotorStatus>& progress) {
	(void)progress;
	starveInterims++;
}


using StarveReqReliable = VUB::ReliableOf<40, 4>;


// The requester and the responder of each queue: the direct stream, a send queue of one, and a send queue of four.
using StarveRequesterDirect = VUB::Stack<+[]() {
	return VUB::Builder()
		.Layer<VUB::Requests<StarveCalls, StarveHandlers>>().Open(4).Tickets(2)
		.Layer<StarveReqReliable>().Retries(3).TimeoutMs(20).Window(4).Counted()
		.Framing<VUB::Cobs>()
		.Link<StarveStream>()
		.MaxPayload(40)
		.ReceiveRing(512)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


using StarveResponderDirect = VUB::Stack<+[]() {
	return VUB::Builder()
		.Layer<VUB::Requests<StarveCalls, StarveHandlers>>().Tickets(2)
		.Layer<StarveReqReliable>().Retries(3).TimeoutMs(20).Window(4).Counted()
		.Framing<VUB::Cobs>()
		.Link<StarveStream>()
		.MaxPayload(40)
		.ReceiveRing(512)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


using StarveRequesterQueue1 = VUB::Stack<+[]() {
	return VUB::Builder()
		.SendQueue(1)
		.Layer<VUB::Requests<StarveCalls, StarveHandlers>>().Open(4).Tickets(2)
		.Layer<StarveReqReliable>().Retries(3).TimeoutMs(20).Window(4).Counted()
		.Framing<VUB::Cobs>()
		.Link<StarveStream>()
		.MaxPayload(40)
		.ReceiveRing(512)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


using StarveResponderQueue1 = VUB::Stack<+[]() {
	return VUB::Builder()
		.SendQueue(1)
		.Layer<VUB::Requests<StarveCalls, StarveHandlers>>().Tickets(2)
		.Layer<StarveReqReliable>().Retries(3).TimeoutMs(20).Window(4).Counted()
		.Framing<VUB::Cobs>()
		.Link<StarveStream>()
		.MaxPayload(40)
		.ReceiveRing(512)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


using StarveRequesterQueue4 = VUB::Stack<+[]() {
	return VUB::Builder()
		.SendQueue(4)
		.Layer<VUB::Requests<StarveCalls, StarveHandlers>>().Open(4).Tickets(2)
		.Layer<StarveReqReliable>().Retries(3).TimeoutMs(20).Window(4).Counted()
		.Framing<VUB::Cobs>()
		.Link<StarveStream>()
		.MaxPayload(40)
		.ReceiveRing(512)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


using StarveResponderQueue4 = VUB::Stack<+[]() {
	return VUB::Builder()
		.SendQueue(4)
		.Layer<VUB::Requests<StarveCalls, StarveHandlers>>().Tickets(2)
		.Layer<StarveReqReliable>().Retries(3).TimeoutMs(20).Window(4).Counted()
		.Framing<VUB::Cobs>()
		.Link<StarveStream>()
		.MaxPayload(40)
		.ReceiveRing(512)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


// The critical sections of this program. The program runs on one task, so a section has no other task to exclude.
void System::CriticalSection(bool isEnable) {
	(void)isEnable;
}


// The requester of a concurrent stack: its start and its fires wait in the layer's queue (Pending), and Process sends them from the layer's own
// Process, ahead of Reliable below. The application fills the queue after each Process.
using StarveConcurrentRequester = VUB::Stack<+[]() {
	return VUB::Builder()
		.SendQueue(1)
		.Layer<VUB::Requests<StarveCalls, StarveHandlers>>().Open(4).Tickets(2).Pending(8)
		.Layer<StarveReqReliable>().Retries(3).TimeoutMs(20).Window(4).Counted()
		.Framing<VUB::Cobs>()
		.Link<StarveStream>()
		.MaxPayload(40)
		.ReceiveRing(512)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>()
		.Concurrent();
}>;


// Requests over Reliable over KeepAlive over a stream: the requests layer is the top, KeepAlive the bottom layer.
using StarveRequesterKeepAlive = VUB::Stack<+[]() {
	return VUB::Builder()
		.CountDrops()
		.Layer<VUB::Requests<StarveCalls, StarveHandlers>>().Open(4).Tickets(2)
		.Layer<StarveReqReliable>().Retries(3).TimeoutMs(20).Window(4).Counted()
		.Layer<VUB::KeepAlive>().QuietTime(1000).DownTime(3000)
		.Framing<VUB::Cobs>()
		.Link<StarveStream>()
		.MaxPayload(40)
		.ReceiveRing(512)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


using StarveResponderKeepAlive = VUB::Stack<+[]() {
	return VUB::Builder()
		.CountDrops()
		.Layer<VUB::Requests<StarveCalls, StarveHandlers>>().Tickets(2)
		.Layer<StarveReqReliable>().Retries(3).TimeoutMs(20).Window(4).Counted()
		.Layer<VUB::KeepAlive>().QuietTime(1000).DownTime(3000)
		.Framing<VUB::Cobs>()
		.Link<StarveStream>()
		.MaxPayload(40)
		.ReceiveRing(512)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


// The stream's frames (COBS, each ends with a zero byte) delivered to the peer, frame by frame, once the delimiter has arrived. While
// lossArmed is set, the first frame of at least 16 bytes is lost whole and the flag is cleared. In the rig the fires are 9-byte frames and a
// request's start is 17 bytes, so the start is the frame lost.
template<typename StackType>
void StarveDeliverLossy(StarveBytes& from, StackType& peer, bool& lossArmed) {
	while (from.head < from.tail && from.head < from.data.size()) {
		std::size_t end = from.head;
		while (end < from.tail && end < from.data.size() && from.data[end] != 0) {
			end++;
		}

		if (end >= from.tail || end >= from.data.size()) {
			return;
		}

		std::size_t length = end - from.head + 1;
		bool lose = lossArmed && length >= 16;
		if (lose) {
			lossArmed = false;
		}

		for (std::size_t index = from.head; index <= end; index++) {
			if (!lose) {
				peer.Feed(from.data[index]);
			}
		}

		from.head = end + 1;
	}
}


// The steps of a start's FINAL when the FINAL never came.
inline constexpr std::size_t noLoss = static_cast<std::size_t>(-1);


// The start of a requester: pending until the requester accepts it, then sent at step.
struct StarveStart {
	bool pending = false;
	bool sent = false;
	std::size_t step = 0;
};


// One step of a requester and a responder over two stream links. The requester's start, while it is pending, is sent first after the
// requester's Process, then the requester calls Fire until busy. The responder does the same with Fire when respStreams is set. Returns
// the fires the requester got accepted; respFires counts the responder's.
template<typename RequesterStack, typename ResponderStack>
std::size_t StarveReqStep(RequesterStack& a, ResponderStack& b, StarveStream& linkA, StarveStream& linkB, StarveBytes& ab, StarveBytes& ba,
	std::size_t perStep, bool respStreams, bool& lossArmed, std::size_t& respFires, std::size_t step, StarveStart& start) {
	testNowUs += 100;
	linkA.StartRound(perStep);
	a.Process();
	std::size_t accepted = 0;
	auto handle = a.Requests();
	if (start.pending) {
		auto sent = handle.template Send<StarveCall::start, &OnStarveFinal, &OnStarveInterim>(Motor::MotorStart{});
		if (!sent.IsErr()) {
			start.pending = false;
			start.sent = true;
			start.step = step;
			lossArmed = true;
		}
	}

	for (std::size_t attempt = 0; attempt < 8; attempt++) {
		if (handle.template Fire<StarveCall::kick>(Motor::MotorFault{}) != ResultStatus::ok) {
			break;
		}

		accepted++;
	}

	StarveDeliverLossy(ab, b, lossArmed);
	linkB.StartRound(perStep);
	b.Process();
	if (respStreams) {
		auto respHandle = b.Requests();
		for (std::size_t attempt = 0; attempt < 8; attempt++) {
			if (respHandle.template Fire<StarveCall::kick>(Motor::MotorFault{}) != ResultStatus::ok) {
				break;
			}

			respFires++;
		}
	}

	StarveDeliver(ba, a);
	return accepted;
}


// The start is sent once the sessions are up, and its first frame loses a byte, so the requester resends it. The requester streams fires in
// every step; the responder streams them too when respStreams is set. Returns the steps from the start's send to its FINAL, or noLoss when
// the FINAL never came. The bound of the steps is written in the tests.
template<typename RequesterStack, typename ResponderStack>
std::size_t StarveStartFinal(bool respStreams, std::size_t& accepted, std::size_t& respFires, std::size_t& resends) {
	ResetTestState();
	starveStarts = 0;
	starveKicks = 0;
	starveFinals = 0;
	starveFinalResult = ResultStatus::notAvailable;
	starveInterims = 0;
	StarveBytes ab;
	StarveBytes ba;
	StarveStream linkA(ab);
	StarveStream linkB(ba);
	RequesterStack a(linkA);
	ResponderStack b(linkB);
	a.template Find<StarveReqReliable>().SetSession(ReliableSession);
	b.template Find<StarveReqReliable>().SetSession(ReliableSession);
	bool lossArmed = false;
	accepted = 0;
	respFires = 0;
	StarveStart start;
	std::size_t finalStep = noLoss;
	for (std::size_t step = 0; step < 1000; step++) {
		if (step == 50) {
			start.pending = true;
		}

		accepted += StarveReqStep(a, b, linkA, linkB, ab, ba, 1, respStreams, lossArmed, respFires, step, start);
		if (start.sent && starveFinals > 0 && finalStep == noLoss) {
			finalStep = step - start.step;
		}
	}

	resends = a.template Find<StarveReqReliable>().Resends();
	SIM_EXPECT(starveStarts == 1);
	SIM_EXPECT(starveFinalResult == ResultStatus::ok);
	return finalStep;
}


// The bound of the FINAL: the link takes one byte a step, a part of a frame in each Process. A lost start is resent when its timeout ends (20 ms,
// 200 steps of 100 us). The start's frame is 17 bytes, so the first send and the resend take 17 steps each, and the reply about 15 steps.
// The FINAL comes about 250 steps after the start. The bound is 400 steps.
inline constexpr std::size_t finalBound = 400;


// The share of the application: the window is 1000 steps, and the link takes one byte a step, so it carries 1000 bytes. A fire is a 9-byte
// frame, so the link carries 111 fires at most. The start, its resend and the reply take about 50 bytes, so an application that gets its
// share takes about 105 of them. The bound is 74: two thirds of the link's fires.
inline constexpr std::size_t shareBound = 74;


template<typename RequesterStack, typename ResponderStack>
void RequestsFinalWhileRequesterStreams(const char* name) {
	std::size_t accepted = 0;
	std::size_t respFires = 0;
	std::size_t resends = 0;
	std::size_t delay = StarveStartFinal<RequesterStack, ResponderStack>(false, accepted, respFires, resends);
	std::printf("%s: FINAL after %zu steps, resends %zu, fires accepted %zu\n", name, delay == noLoss ? 0 : delay, resends, accepted);
	SIM_EXPECT(delay != noLoss);
	SIM_EXPECT(delay <= finalBound);
	SIM_EXPECT(resends >= 1);
	SIM_EXPECT(accepted >= shareBound);
}


void TestRequestsFinalWhileStreamingDirect() {
	RequestsFinalWhileRequesterStreams<StarveRequesterDirect, StarveResponderDirect>("requests direct");
}


void TestRequestsFinalWhileStreamingQueue1() {
	RequestsFinalWhileRequesterStreams<StarveRequesterQueue1, StarveResponderQueue1>("requests queue 1");
}


void TestRequestsFinalWhileStreamingQueue4() {
	RequestsFinalWhileRequesterStreams<StarveRequesterQueue4, StarveResponderQueue4>("requests queue 4");
}


// The concurrent requester: its layer sends its own queue from Process, so Reliable below is the layer that the layer above starves.
void TestRequestsFinalWhileStreamingConcurrent() {
	RequestsFinalWhileRequesterStreams<StarveConcurrentRequester, StarveResponderDirect>("requests concurrent");
}


// The responder streams fires while its FINAL goes to the requester: the FINAL comes within the same bound.
template<typename RequesterStack, typename ResponderStack>
void ResponderFinalWhileStreaming(const char* name) {
	std::size_t accepted = 0;
	std::size_t respFires = 0;
	std::size_t resends = 0;
	std::size_t delay = StarveStartFinal<RequesterStack, ResponderStack>(true, accepted, respFires, resends);
	std::printf("%s: FINAL after %zu steps, responder fires accepted %zu\n", name, delay == noLoss ? 0 : delay, respFires);
	SIM_EXPECT(delay != noLoss);
	SIM_EXPECT(delay <= finalBound);
	SIM_EXPECT(respFires >= shareBound);
}


void TestResponderFinalWhileStreamingDirect() {
	ResponderFinalWhileStreaming<StarveRequesterDirect, StarveResponderDirect>("responder direct");
}


void TestResponderFinalWhileStreamingQueue1() {
	ResponderFinalWhileStreaming<StarveRequesterQueue1, StarveResponderQueue1>("responder queue 1");
}


void TestResponderFinalWhileStreamingQueue4() {
	ResponderFinalWhileStreaming<StarveRequesterQueue4, StarveResponderQueue4>("responder queue 4");
}


// KeepAlive below Reliable below Requests, with the requester and the responder both streaming fires: the peer stays up for 600 steps (60 ms,
// fifteen times the 3 ms down time that is written as 3000 us in the chain).
void TestThreeBusyLayersKeepPeerUp() {
	ResetTestState();
	StarveBytes ab;
	StarveBytes ba;
	StarveStream linkA(ab);
	StarveStream linkB(ba);
	StarveRequesterKeepAlive a(linkA);
	StarveResponderKeepAlive b(linkB);
	a.template Find<StarveReqReliable>().SetSession(ReliableSession);
	b.template Find<StarveReqReliable>().SetSession(ReliableSession);
	bool lossArmed = false;
	std::size_t accepted = 0;
	std::size_t respFires = 0;
	StarveStart start;
	for (std::size_t step = 0; step < 600; step++) {
		accepted += StarveReqStep(a, b, linkA, linkB, ab, ba, 3, true, lossArmed, respFires, step, start);
		SIM_EXPECT(a.LinkUp());
		SIM_EXPECT(b.LinkUp());
	}

	std::printf("three busy layers: fires accepted %zu, responder fires %zu\n", accepted, respFires);
	SIM_EXPECT(accepted >= 120);
	SIM_EXPECT(respFires >= 120);
}


// A byte-stream link that runs starveWriteHook when the stack hands it a frame, before the frame's bytes are taken.
inline void (*starveWriteHook)() = nullptr;


class StarveHookStream : public StarveStream {
public:
	static constexpr VUB::LinkTraits traits = VUB::LinkTraits().MaxPacket(64).HalfDuplex();


	explicit StarveHookStream(StarveBytes& outgoing) : StarveStream(outgoing) {
	}


	Result<std::size_t> Write(std::span<const uint8> bytes) {
		if (starveWriteHook != nullptr) {
			starveWriteHook();
		}

		return StarveStream::Write(bytes);
	}
};


// The concurrent requester of the refill test: its requests queue sends from the layer's Process, and its link runs the hook.
using StarveHookRequester = VUB::Stack<+[]() {
	return VUB::Builder()
		.SendQueue(1)
		.Layer<VUB::Requests<StarveCalls, StarveHandlers>>().Open(4).Tickets(2).Pending(8)
		.Layer<StarveReqReliable>().Retries(3).TimeoutMs(20).Window(4).Counted()
		.Framing<VUB::Cobs>()
		.Link<StarveHookStream>()
		.MaxPayload(40)
		.ReceiveRing(512)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>()
		.Concurrent();
}>;


// The requester the hook fills. It is set while the refill test runs.
inline StarveHookRequester* starveHookRequester = nullptr;


// Fires into the requester's queue until it answers busy, so that the requests layer has a packet at its next Process.
void RefillHookRequester() {
	if (starveHookRequester == nullptr) {
		return;
	}

	auto handle = starveHookRequester->Requests();
	for (std::size_t attempt = 0; attempt < 16; attempt++) {
		if (handle.template Fire<StarveCall::kick>(Motor::MotorFault{}) != ResultStatus::ok) {
			break;
		}
	}
}


// One step of the refill test. The requester's Process runs, the start is sent if it is pending, and the queue is refilled again, so it holds
// a packet at the next Process. The start is sent first, at step 0, before the refills take the queue: a refill in the link's Write takes
// every slot that a sent frame frees. The link carries three bytes a step, and the first frame of 16 bytes or more after the start was sent
// is lost.
void StarveHookStep(StarveHookRequester& a, StarveResponderDirect& b, StarveHookStream& linkA, StarveStream& linkB, StarveBytes& ab,
	StarveBytes& ba, bool& lossArmed, StarveStart& start, std::size_t step) {
	testNowUs += 100;
	linkA.StartRound(3);
	a.Process();
	if (start.pending) {
		auto handle = a.Requests();
		auto sent = handle.template Send<StarveCall::start, &OnStarveFinal, &OnStarveInterim>(Motor::MotorStart{});
		if (!sent.IsErr()) {
			start.pending = false;
			start.sent = true;
			start.step = step;
			lossArmed = true;
		}
	}

	RefillHookRequester();
	StarveDeliverLossy(ab, b, lossArmed);
	linkB.StartRound(3);
	b.Process();
	StarveDeliver(ba, a);
}


// The refill test: the requests queue holds a request at every Process of the requester and in every Write of its link, so its requests layer
// has a packet whenever its Process runs. The start's first frame is lost, so Reliable must resend it through a link that the requests layer
// would otherwise hold. The FINAL comes within the bound, and the requester's fires still get a share.
void TestRequestsRefilledAtEveryProcessResend() {
	ResetTestState();
	starveStarts = 0;
	starveKicks = 0;
	starveFinals = 0;
	starveFinalResult = ResultStatus::notAvailable;
	starveInterims = 0;
	StarveBytes ab;
	StarveBytes ba;
	StarveHookStream linkA(ab);
	StarveStream linkB(ba);
	StarveHookRequester a(linkA);
	StarveResponderDirect b(linkB);
	a.template Find<StarveReqReliable>().SetSession(ReliableSession);
	b.template Find<StarveReqReliable>().SetSession(ReliableSession);
	starveHookRequester = &a;
	starveWriteHook = &RefillHookRequester;
	bool lossArmed = false;
	StarveStart start;
	start.pending = true;
	std::size_t finalStep = noLoss;
	for (std::size_t step = 0; step < 1000; step++) {
		StarveHookStep(a, b, linkA, linkB, ab, ba, lossArmed, start, step);
		if (start.sent && starveFinals > 0 && finalStep == noLoss) {
			finalStep = step - start.step;
		}
	}

	starveWriteHook = nullptr;
	starveHookRequester = nullptr;
	std::size_t resends = a.template Find<StarveReqReliable>().Resends();
	std::printf("refilled requests: FINAL after %zu steps, resends %zu, fires kicked %zu\n", finalStep == noLoss ? 0 : finalStep, resends,
		starveKicks);
	SIM_EXPECT(starveStarts == 1);
	SIM_EXPECT(finalStep != noLoss);
	SIM_EXPECT(finalStep <= finalBound);
	SIM_EXPECT(resends >= 1);
	SIM_EXPECT(starveKicks >= shareBound);
}


// An upper layer that sends one packet in each Process, for as long as the layer has ids to send. The packet is 20 bytes and begins with a
// 16-bit id, so Reliable's outcomes count it by id.
struct StarveEveryLayer {
	static constexpr std::size_t header = 0;
	static constexpr uint32 fingerprint = 0x53455631;
	static constexpr bool usesTime = false;
	static constexpr bool makesBulk = false;


	Result<std::size_t> Add(const VUB::Meta&, std::span<const uint8> in, std::span<uint8> out) {
		if (out.size() < in.size()) {
			return ResultStatus::invalidParameter;
		}

		for (std::size_t index = 0; index < in.size(); index++) {
			out[index] = in[index];
		}

		return in.size();
	}


	Result<VUB::Taken> Take(const VUB::Meta&, std::span<const uint8>) {
		return VUB::Taken{0, true};
	}


	void Process(uint32, const VUB::Outbox& outbox) {
		if (!pending && nextId < everyIds) {
			packet[0] = static_cast<uint8>(nextId & 0xFF);
			packet[1] = static_cast<uint8>(nextId >> 8);
			nextId++;
			pending = true;
		}

		if (!pending) {
			return;
		}

		if (outbox.Send(std::span<const uint8>(packet.data(), packet.size())) == ResultStatus::ok) {
			pending = false;
			sent++;
		} else {
			refused++;
		}
	}


	static constexpr uint16 everyIds = 500;
	std::array<uint8, 20> packet{};
	uint16 nextId = 0;
	bool pending = false;
	uint32 sent = 0;
	uint32 refused = 0;
};


// Reliable of the layer test. Its window is 16, as StarveReliable has: the upper layer keeps sending while a packet is lost and its resend
// waits, so the upper layer reaches the link and the resend has to get through it.
using StarveEveryReliable = VUB::ReliableOf<40, 16>;


// The upper layer of the layer test over Reliable over a byte stream. Reliable reports each outcome by the id of its packet.
using StarveEveryDirect = VUB::Stack<+[]() {
	return VUB::Builder()
		.Layer<StarveEveryLayer>()
		.Layer<StarveEveryReliable>().Retries(3).TimeoutMs(20).Window(16).PayloadBytes(40).OnOutcome<&ReliableOutcome>().Counted()
		.Framing<VUB::Cobs>()
		.Link<StarveStream>()
		.MaxPayload(40)
		.ReceiveRing(512)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


// The peer of the layer test: Reliable with the delivery hook, and nothing above it.
using StarveEveryPeer = VUB::Stack<+[]() {
	return VUB::Builder()
		.Layer<StarveEveryReliable>().Retries(3).TimeoutMs(20).Window(16).PayloadBytes(40)
		.Framing<VUB::Cobs>()
		.Link<StarveStream>()
		.MaxPayload(40)
		.ReceiveRing(512)
		.OnReceive<&ReliableReceive>()
		.Clock<&TestClock>();
}>;


// The layer test: the upper layer sends in every Process, over Reliable over a byte stream whose first frame of 16 bytes or more is lost. The
// lost packet must be resent through the link within the bound, and the upper layer must still get a share of the link.
void TestUpperLayerSendsEveryProcessResendGoes() {
	ResetReliableState();
	StarveBytes ab;
	StarveBytes ba;
	StarveStream linkA(ab);
	StarveStream linkB(ba);
	StarveEveryDirect a(linkA);
	StarveEveryPeer b(linkB);
	a.template Find<StarveEveryReliable>().SetSession(ReliableSession);
	b.template Find<StarveEveryReliable>().SetSession(ReliableSession);
	bool lossArmed = true;
	std::size_t lossStep = noLoss;
	for (std::size_t step = 0; step < 1000; step++) {
		testNowUs += 100;
		linkA.StartRound(3);
		a.Process();
		bool wasArmed = lossArmed;
		StarveDeliverLossy(ab, b, lossArmed);
		if (wasArmed && !lossArmed && lossStep == noLoss) {
			lossStep = step;
		}

		linkB.StartRound(3);
		b.Process();
		StarveDeliver(ba, a);
	}

	std::size_t resendOutcome = noLoss;
	std::size_t delivered = 0;
	for (std::size_t id = 0; id < ReliableIds; id++) {
		if (testDelivered[id] > 0) {
			delivered++;
		}

		if (resendOutcome == noLoss && testAttempts[id] >= 2 && testStatus[id] == ResultStatus::ok) {
			resendOutcome = id;
		}
	}

	std::size_t resendSteps = resendOutcome == noLoss ? noLoss : static_cast<std::size_t>(testOutcomeAtUs[resendOutcome] / 100) - lossStep;
	std::printf("upper layer every process: lost at step %zu, resent id %zu after %zu steps, ids delivered %zu\n", lossStep,
		resendOutcome == noLoss ? 0 : resendOutcome, resendSteps == noLoss ? 0 : resendSteps, delivered);
	SIM_EXPECT(lossStep != noLoss);
	SIM_EXPECT(resendOutcome != noLoss);
	SIM_EXPECT(resendSteps <= finalBound);
	SIM_EXPECT(delivered >= 30);
}


int main() {
	TestRequestsRefilledAtEveryProcessResend();
	TestUpperLayerSendsEveryProcessResendGoes();
	TestRequestsFinalWhileStreamingDirect();
	TestRequestsFinalWhileStreamingQueue1();
	TestRequestsFinalWhileStreamingQueue4();
	TestRequestsFinalWhileStreamingConcurrent();
	TestResponderFinalWhileStreamingDirect();
	TestResponderFinalWhileStreamingQueue1();
	TestResponderFinalWhileStreamingQueue4();
	TestThreeBusyLayersKeepPeerUp();
	TestKeepAliveStaysUpWhileStreaming();
	TestKeepAliveStaysUpQueuedWhileStreaming();
	TestSpamLayerGetsLinkWhileStreamingDirect();
	TestSpamLayerGetsLinkWhileStreamingQueue1();
	TestSpamLayerGetsLinkWhileStreamingQueue4();
	TestReliableResendWhileStreaming();
	TestBusySendTakesNothingFromReliable();
	TestKeepAlivePingGoesWhilePeerDownWait();
	TestKeepAlivePingGoesWhilePeerDownFail();
	return SimFinish("TestVubStackStarvation");
}
