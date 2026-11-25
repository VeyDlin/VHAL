#include "Chains.h"
#include "ReliableChains.h"
#include "SimCheck.h"
#include <Drivers/Interface/VUB/Framing/Cobs.h>
#include <Drivers/Interface/VUB/Layer/KeepAlive.h>
#include <Drivers/Interface/VUB/LinkTraits.h>
#include <Drivers/Interface/VUB/Meta.h>
#include <Drivers/Interface/VUB/Outbox.h>
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


int main() {
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
