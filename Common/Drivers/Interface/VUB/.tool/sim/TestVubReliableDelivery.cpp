#include "SimCheck.h"
#include "VubReliableChains.h"
#include <Drivers/Interface/VUB/TestKit/VubLoopback.h>
#include <Utilities/DataTypes.h>
#include <array>
#include <cstddef>
#include <span>
#include <vector>


// Acknowledgement, duplicates, the window, the retries and their timing, backoff, sequence wraparound, and packets that are not
// acknowledged. These lines lose nothing except where a test says so.


// One round of a packet line: the near stack processes, its packets reach the far stack, the far stack processes, and its
// acknowledgements come back.
template<typename Line, typename Stack>
void PacketRound(Line& line, Stack& near, Stack& far) {
	near.Process();
	line.DeliverToB(far);
	far.Process();
	line.DeliverToA(near);
	near.Process();
}


// Starts the session. The first Send is busy and starts it: the data waits for the sync's acknowledgement, which one round brings back.
template<typename Line, typename Stack>
void Establish(Line& line, Stack& near, Stack& far) {
	SIM_EXPECT(ReliableSend(near, 0xFFFF) == ResultStatus::busy);
	PacketRound(line, near, far);
}


// Every packet of ids 1 to 3 is delivered once, in order, and each gets one outcome: ok.
void TestSendIsDeliveredAndAcknowledged() {
	ResetReliableState();
	PacketLine line(VubFaults{});
	ReliableWire near(line.a);
	GiveSession(near);
	ReliableWire far(line.b);
	GiveSession(far);
	near.Process();
	far.Process();
	Establish(line, near, far);
	SIM_EXPECT(ReliableSend(near, 1) == ResultStatus::ok);
	SIM_EXPECT(ReliableSend(near, 2) == ResultStatus::ok);
	SIM_EXPECT(ReliableSend(near, 3) == ResultStatus::ok);
	for (std::size_t round = 0; round < 4; round++) {
		PacketRound(line, near, far);
	}

	for (uint16 id = 1; id <= 3; id++) {
		SIM_EXPECT(testDelivered[id] == 1);
		SIM_EXPECT(testOutcomes[id] == 1);
		SIM_EXPECT(testStatus[id] == ResultStatus::ok);
	}

	SIM_EXPECT(testRestarts == 0);
}


// The window: four packets may be unacknowledged; the fifth is refused with busy until an acknowledgement frees a place.
void TestWindowLimitsUnacknowledgedPackets() {
	ResetReliableState();
	PacketLine line(VubFaults{});
	ReliableWire near(line.a);
	GiveSession(near);
	ReliableWire far(line.b);
	GiveSession(far);
	near.Process();
	far.Process();
	Establish(line, near, far);
	for (uint16 id = 1; id <= 4; id++) {
		SIM_EXPECT(ReliableSend(near, id) == ResultStatus::ok);
	}

	SIM_EXPECT(ReliableSend(near, 5) == ResultStatus::busy);
	PacketRound(line, near, far);
	SIM_EXPECT(ReliableSend(near, 5) == ResultStatus::ok);
	PacketRound(line, near, far);
	SIM_EXPECT(testDelivered[5] == 1);
	SIM_EXPECT(testOutcomes[5] == 1);
}


// A packet that arrives twice is delivered once. It is acknowledged both times, since the first acknowledgement may have been the one
// lost.
void TestDuplicateIsDeliveredOnce() {
	ResetReliableState();
	PacketLine line(VubFaults{});
	ReliableWire near(line.a);
	GiveSession(near);
	ReliableWire far(line.b);
	GiveSession(far);
	near.Process();
	far.Process();
	Establish(line, near, far);
	SIM_EXPECT(ReliableSend(near, 7) == ResultStatus::ok);
	near.Process();
	ReliableCapture capture;
	line.DeliverToB(capture);
	SIM_EXPECT(capture.count == 1);
	std::span<const uint8> copy(capture.packets[0].data(), capture.lengths[0]);
	far.FeedPacket(copy);
	far.FeedPacket(copy);
	far.Process();
	SIM_EXPECT(testDelivered[7] == 1);
	ReliableCapture acks;
	line.DeliverToA(acks);
	SIM_EXPECT(acks.count == 2);
	near.FeedPacket(std::span<const uint8>(acks.packets[0].data(), acks.lengths[0]));
	near.FeedPacket(std::span<const uint8>(acks.packets[1].data(), acks.lengths[1]));
	near.Process();
	SIM_EXPECT(testOutcomes[7] == 1);
	SIM_EXPECT(testStatus[7] == ResultStatus::ok);
}


// The data of a session is sent with a loss on every line after the session starts: the packet is sent at 0, resent every 200 ms, and
// after the last resend (the fourth try, with three retries) the timeout at 800 ms reports retryExhausted. Nothing is reported before
// that. The clock is in microseconds: the loop steps 1 ms. The line drops every packet that is pushed after the session has started, so
// the sent times are the times of the packet's transmissions.
void TestRetriesAtTheRightTimes() {
	ResetReliableState();
	PacketLine line(VubFaults{});
	ReliableWire near(line.a);
	GiveSession(near);
	ReliableWire far(line.b);
	GiveSession(far);
	testNowUs = 0;
	line.SetNow(0);
	near.Process();
	far.Process();
	Establish(line, near, far);
	SIM_EXPECT(ReliableSend(near, 9) == ResultStatus::ok);
	ReliableCapture discard;
	std::vector<uint32> sentAt;
	uint32 pushedBefore = line.toB.Counts().pushed;
	for (uint32 now = 0; now <= 800000; now += 1000) {
		testNowUs = now;
		line.SetNow(now);
		near.Process();
		uint32 pushed = line.toB.Counts().pushed;
		if (pushed != pushedBefore) {
			sentAt.push_back(now);
			pushedBefore = pushed;
		}

		line.DeliverToB(discard);
		if (now < 800000) {
			SIM_EXPECT(testOutcomes[9] == 0);
		}
	}

	SIM_EXPECT(sentAt.size() == 4);
	if (sentAt.size() == 4) {
		SIM_EXPECT(sentAt[0] == 0);
		SIM_EXPECT(sentAt[1] == 200000);
		SIM_EXPECT(sentAt[2] == 400000);
		SIM_EXPECT(sentAt[3] == 600000);
	}

	SIM_EXPECT(testOutcomes[9] == 1);
	SIM_EXPECT(testStatus[9] == ResultStatus::retryExhausted);
	SIM_EXPECT(testOutcomeAtUs[9] == 800000);
	SIM_EXPECT(testAttempts[9] == 4);
}


// With backoff to 800 ms the timeout doubles after each try: sends at 0, 200 ms, 600 ms and 1400 ms, then the timeout of 800 ms after the
// fourth try reports retryExhausted at 2200 ms. As above, the line drops every packet after the session has started.
void TestBackoffTiming() {
	ResetReliableState();
	PacketLine line(VubFaults{});
	ReliableBackoff near(line.a);
	GiveSession(near);
	ReliableBackoff far(line.b);
	GiveSession(far);
	testNowUs = 0;
	line.SetNow(0);
	near.Process();
	far.Process();
	Establish(line, near, far);
	SIM_EXPECT(ReliableSend(near, 9) == ResultStatus::ok);
	ReliableCapture discard;
	std::vector<uint32> sentAt;
	uint32 pushedBefore = line.toB.Counts().pushed;
	for (uint32 now = 0; now <= 2200000; now += 1000) {
		testNowUs = now;
		line.SetNow(now);
		near.Process();
		uint32 pushed = line.toB.Counts().pushed;
		if (pushed != pushedBefore) {
			sentAt.push_back(now);
			pushedBefore = pushed;
		}

		line.DeliverToB(discard);
	}

	SIM_EXPECT(sentAt.size() == 4);
	if (sentAt.size() == 4) {
		SIM_EXPECT(sentAt[0] == 0);
		SIM_EXPECT(sentAt[1] == 200000);
		SIM_EXPECT(sentAt[2] == 600000);
		SIM_EXPECT(sentAt[3] == 1400000);
	}

	SIM_EXPECT(testStatus[9] == ResultStatus::retryExhausted);
	SIM_EXPECT(testOutcomeAtUs[9] == 2200000);
	SIM_EXPECT(testOutcomes[9] == 1);
}


// A packet given up after its resends does not stop the packets after it: the line is down while packet 1 is sent and resent, so it is
// exhausted at 800 ms, and the packets 2 to 21 sent after the line is up are all delivered once. The receiver moves past the gap.
void TestGivenUpPacketDoesNotStopTheStream() {
	ResetReliableState();
	PacketLine line(VubFaults{});
	ReliableWire near(line.a);
	GiveSession(near);
	ReliableWire far(line.b);
	GiveSession(far);
	testNowUs = 0;
	line.SetNow(0);
	near.Process();
	far.Process();
	Establish(line, near, far);
	SIM_EXPECT(ReliableSend(near, 0) == ResultStatus::ok);
	PacketRound(line, near, far);
	PacketRound(line, near, far);
	SIM_EXPECT(testOutcomes[0] == 1);
	line.SetLinkUp(false);
	SIM_EXPECT(ReliableSend(near, 1) == ResultStatus::ok);
	uint16 next = 2;
	for (uint32 now = 1000; now <= 3000000; now += 1000) {
		testNowUs = now;
		line.SetNow(now);
		if (now == 900000) {
			line.SetLinkUp(true);
		}

		if (now >= 900000 && next < 22 && ReliableSend(near, next) == ResultStatus::ok) {
			next++;
		}

		PacketRound(line, near, far);
	}

	SIM_EXPECT(testOutcomes[1] == 1);
	SIM_EXPECT(testStatus[1] == ResultStatus::retryExhausted);
	SIM_EXPECT(testDelivered[1] == 0);
	for (uint16 id = 2; id < 22; id++) {
		SIM_EXPECT(testOutcomes[id] == 1);
		SIM_EXPECT(testStatus[id] == ResultStatus::ok);
		SIM_EXPECT(testDelivered[id] == 1);
	}

	SIM_EXPECT(testRestarts == 0);
}


// Eight-bit sequence numbers wrap around every 256 packets, and 500 packets in a row are each delivered once and acknowledged.
void TestEightBitSequenceWraps() {
	ResetReliableState();
	PacketLine line(VubFaults{});
	ReliableWire8 near(line.a);
	GiveSession(near);
	ReliableWire8 far(line.b);
	GiveSession(far);
	near.Process();
	far.Process();
	uint16 sent = 0;
	for (std::size_t round = 0; round < 2000 && sent < 500; round++) {
		if (ReliableSend(near, sent) == ResultStatus::ok) {
			sent++;
		}

		PacketRound(line, near, far);
	}

	for (std::size_t round = 0; round < 8; round++) {
		PacketRound(line, near, far);
	}

	SIM_EXPECT(sent == 500);
	for (uint16 id = 0; id < 500; id++) {
		SIM_EXPECT(testDelivered[id] == 1);
		SIM_EXPECT(testOutcomes[id] == 1);
	}

	SIM_EXPECT(testRestarts == 0);
}


// A packet with the no-acknowledgement flag is delivered, gets no outcome, and gets no acknowledgement back.
void TestUnsequencedPacketIsNotAcknowledged() {
	ResetReliableState();
	PacketLine line(VubFaults{});
	ReliableWire near(line.a);
	GiveSession(near);
	ReliableWire far(line.b);
	GiveSession(far);
	near.Process();
	far.Process();
	Establish(line, near, far);
	ReliableCapture drained;
	line.DeliverToA(drained);
	SIM_EXPECT(ReliableSend(near, 11, VubMeta::NoAcknowledgeFlag) == ResultStatus::ok);
	PacketRound(line, near, far);
	PacketRound(line, near, far);
	SIM_EXPECT(testDelivered[11] == 1);
	SIM_EXPECT(testOutcomes[11] == 0);
	ReliableCapture backToNear;
	line.DeliverToA(backToNear);
	SIM_EXPECT(backToNear.count == 0);
	SIM_EXPECT(near.Drops().layerError == 0);
}


int main() {
	TestSendIsDeliveredAndAcknowledged();
	TestWindowLimitsUnacknowledgedPackets();
	TestDuplicateIsDeliveredOnce();
	TestRetriesAtTheRightTimes();
	TestBackoffTiming();
	TestEightBitSequenceWraps();
	TestUnsequencedPacketIsNotAcknowledged();
	TestGivenUpPacketDoesNotStopTheStream();
	return SimFinish("TestVubReliableDelivery");
}
