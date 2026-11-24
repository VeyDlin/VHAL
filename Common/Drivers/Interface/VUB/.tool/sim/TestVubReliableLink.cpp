#include "SimCheck.h"
#include "ReliableChains.h"
#include <Drivers/Interface/VUB/TestKit/Loopback.h>
#include <Utilities/DataTypes.h>
#include <cstddef>
#include <span>


// The link state under the delivery layer. While the connection is down the layer neither resends nor spends retries, and on up it
// resends what is pending at once. The clock is in microseconds; the loops here step 1 ms.


// One round of a packet line: the near stack processes, its packets reach the far stack, and its answers come back.
template<typename Line, typename Stack>
void PacketRoundOnce(Line& line, Stack& near, Stack& far) {
	near.Process();
	line.DeliverToB(far);
	far.Process();
	line.DeliverToA(near);
	near.Process();
}


// An outage from 50 ms to 5000 ms, far longer than the retries (800 ms) would last. A packet sent before the outage is not
// acknowledged. Nothing is sent or counted while the line is down, nothing fails, and on up the packet is resent at once.
void TestOutagePausesRetriesAndResendsOnUp() {
	ResetReliableState();
	ReportingPacketLine line(VUB::Faults{});
	ReliablePacket near(line.a);
	GiveSession(near);
	ReliablePacket far(line.b);
	GiveSession(far);
	testNowUs = 0;
	line.SetNow(0);
	near.Process();
	far.Process();
	SIM_EXPECT(ReliableSend(near, 0xFFFF) == ResultStatus::busy);
	PacketRoundOnce(line, near, far);
	SIM_EXPECT(ReliableSend(near, 0) == ResultStatus::ok);
	near.Process();
	// The sync and the first transmission of packet 0 are the two packets sent so far.
	SIM_EXPECT(line.toB.Counts().pushed == 2);

	uint32 pushedAtDown = 0;
	for (uint32 now = 1000; now <= 5000000; now += 1000) {
		testNowUs = now;
		line.SetNow(now);
		if (now == 50000) {
			line.SetLinkUp(false);
			pushedAtDown = line.toB.Counts().pushed;
		}

		if (now == 5000000) {
			line.SetLinkUp(true);
		}

		near.Process();
		if (now > 50000 && now < 5000000) {
			SIM_EXPECT(line.toB.Counts().pushed == pushedAtDown);
			SIM_EXPECT(testOutcomes[0] == 0);
		}
	}

	SIM_EXPECT(pushedAtDown == 2);
	SIM_EXPECT(line.toB.Counts().pushed == 3);
	line.DeliverToB(far);
	far.Process();
	line.DeliverToA(near);
	near.Process();
	SIM_EXPECT(testDelivered[0] == 1);
	SIM_EXPECT(testOutcomes[0] == 1);
	SIM_EXPECT(testStatus[0] == ResultStatus::ok);
	SIM_EXPECT(testAttempts[0] == 2);
	SIM_EXPECT(testOutcomeAtUs[0] == 5000000);
}


// A packet sent while the line is down waits in the stack's queue and goes out when the line is up. The packet is delivered once, and
// its outcome is ok.
void TestPacketSentWhileDownIsDeliveredOnceOnUp() {
	ResetReliableState();
	ReportingPacketLine line(VUB::Faults{});
	ReliablePacket near(line.a);
	GiveSession(near);
	ReliablePacket far(line.b);
	GiveSession(far);
	testNowUs = 0;
	line.SetNow(0);
	near.Process();
	far.Process();
	SIM_EXPECT(ReliableSend(near, 0xFFFF) == ResultStatus::busy);
	PacketRoundOnce(line, near, far);
	line.SetLinkUp(false);
	near.Process();
	SIM_EXPECT(!near.LinkUp());
	SIM_EXPECT(ReliableSend(near, 1) == ResultStatus::ok);
	uint32 pushedDown = line.toB.Counts().pushed;
	for (uint32 now = 1000; now <= 1000000; now += 1000) {
		testNowUs = now;
		line.SetNow(now);
		near.Process();
	}

	SIM_EXPECT(testOutcomes[1] == 0);
	SIM_EXPECT(line.toB.Counts().pushed == pushedDown);
	testNowUs = 1000000;
	line.SetLinkUp(true);
	near.Process();
	SIM_EXPECT(near.LinkUp());
	for (std::size_t round = 0; round < 3; round++) {
		line.DeliverToB(far);
		far.Process();
		line.DeliverToA(near);
		near.Process();
	}

	SIM_EXPECT(testDelivered[1] == 1);
	SIM_EXPECT(testOutcomes[1] == 1);
	SIM_EXPECT(testStatus[1] == ResultStatus::ok);
	SIM_EXPECT(testRestarts == 0);
}


// A layer above the delivery layer that declares DeliveryWanted: the test sets its answer, and the count of its questions is kept.
inline bool wantedAnswer = true;
inline std::size_t wantedAsked = 0;


struct WantingLayer {
	static constexpr std::size_t header = 0;
	static constexpr uint32 fingerprint = 0x57414E54;
	static constexpr bool usesTime = false;
	static constexpr bool makesBulk = false;
	static constexpr bool wantsOwner = true;


	static void DeliveryOutcome(const VUB::Outcome& outcome) {
		ReliableOutcome(outcome);
	}


	static void DeliveryRestart(void*) {
		ReliableRestart();
	}


	static bool DeliveryWanted(void*, std::span<const uint8>) {
		wantedAsked++;
		return wantedAnswer;
	}


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


	void Process(uint32, const VUB::Outbox&) {
	}
};


// A sink that takes what a line delivers and passes none of it on: the packet is lost.
struct Discard {
	void Feed(uint8) {
	}


	void FeedPacket(std::span<const uint8>) {
	}
};


// The owner layer over the delivery layer, in its two orders.
using WantingOutChain = VUB::Stack<+[]() {
	return VUB::Builder()
		.Layer<WantingLayer>()
		.Layer<VUB::ReliableOf<40, 4>>().Retries(3).TimeoutMs(200).Window(4)
		.Link<ReportingPacketEnd>()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.OnReceive<&ReliableReceive>()
		.Clock<&TestClock>();
}>;


using WantingInOrderChain = VUB::Stack<+[]() {
	return VUB::Builder()
		.Layer<WantingLayer>()
		.Layer<VUB::ReliableOf<40, 4, true>>().Retries(3).TimeoutMs(200).Window(4)
		.Link<ReportingPacketEnd>()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.OnReceive<&ReliableReceive>()
		.Clock<&TestClock>();
}>;


// A packet whose first send is lost, and whose link goes down before its resend: on up, the layer above no longer wants it, so the resend
// is not sent and its outcome is operationCancelled. The packets after it are delivered; packet 0 is not, and its gap is passed.
template<typename Chain, typename Layer>
void RunNotWantedAfterOutage() {
	ResetReliableState();
	wantedAnswer = true;
	wantedAsked = 0;
	ReportingPacketLine line(VUB::Faults{});
	Chain near(line.a);
	near.template Find<Layer>().SetSession(ReliableSession);
	Chain far(line.b);
	far.template Find<Layer>().SetSession(ReliableSession);
	testNowUs = 0;
	line.SetNow(0);
	near.Process();
	far.Process();
	SIM_EXPECT(ReliableSend(near, 0xFFFF) == ResultStatus::busy);
	PacketRoundOnce(line, near, far);
	SIM_EXPECT(ReliableSend(near, 0) == ResultStatus::ok);
	near.Process();
	Discard discard;
	line.DeliverToB(discard);
	uint32 pushedBefore = line.toB.Counts().pushed;

	line.SetLinkUp(false);
	wantedAnswer = false;
	for (uint32 now = 1000; now <= 3000000; now += 1000) {
		testNowUs = now;
		line.SetNow(now);
		near.Process();
	}

	SIM_EXPECT(line.toB.Counts().pushed == pushedBefore);
	SIM_EXPECT(testOutcomes[0] == 0);
	testNowUs = 3000000;
	line.SetNow(3000000);
	line.SetLinkUp(true);
	near.Process();
	near.Process();
	SIM_EXPECT(wantedAsked == 1);
	SIM_EXPECT(testOutcomes[0] == 1);
	SIM_EXPECT(testStatus[0] == ResultStatus::operationCancelled);
	SIM_EXPECT(line.toB.Counts().pushed == pushedBefore);

	wantedAnswer = true;
	ReliableSend(near, 1);
	for (uint32 round = 0; round < 3; round++) {
		testNowUs += 1000;
		line.SetNow(testNowUs);
		PacketRoundOnce(line, near, far);
	}

	SIM_EXPECT(testDelivered[0] == 0);
	SIM_EXPECT(testDelivered[1] == 1);
	SIM_EXPECT(testOutcomes[1] == 1);
	SIM_EXPECT(testStatus[1] == ResultStatus::ok);
	SIM_EXPECT(testRestarts == 0);
}


// Random outages on a reporting line that also loses and delays packets. Packets are sent 20 ms apart, so some are sent into an
// outage. Every packet gets one outcome, and an ok outcome means delivery exactly once. The count of failed outcomes is not asserted:
// an acknowledgement lost in each of four tries in a row would fail a packet, and the test does not claim that cannot happen.
void TestRandomOutagesKeepExactlyOnce() {
	ResetReliableState();
	VUB::Faults faults;
	faults.seed = 0x7777;
	faults.dropPercent = 2;
	faults.maxDelayUs = 100;
	faults.outageEveryUs = 900000;
	faults.outageForUs = 300000;
	ReportingPacketLine line(faults);
	ReliablePacket near(line.a);
	GiveSession(near);
	ReliablePacket far(line.b);
	GiveSession(far);
	near.Process();
	far.Process();
	const std::size_t total = 80;
	std::size_t sent = 0;
	uint32 nextSendUs = 0;
	uint32 now = 0;
	bool sawDown = false;
	while (now < 120000000) {
		now += 500;
		testNowUs = now;
		line.SetNow(now);
		if (sent < total && now >= nextSendUs && ReliableSend(near, static_cast<uint16>(sent)) == ResultStatus::ok) {
			sent++;
			nextSendUs = now + 20000;
		}

		near.Process();
		sawDown = sawDown || !near.LinkUp();
		line.DeliverToB(far);
		far.Process();
		line.DeliverToA(near);
		near.Process();
		std::size_t settled = 0;
		for (std::size_t id = 0; id < total; id++) {
			if (testOutcomes[id] == 1) {
				settled++;
			}
		}

		if (sent == total && settled == total) {
			break;
		}
	}

	std::size_t okCount = 0;
	for (std::size_t id = 0; id < total; id++) {
		SIM_EXPECT(testOutcomes[id] == 1);
		SIM_EXPECT(testDelivered[id] <= 1);
		if (testStatus[id] == ResultStatus::ok) {
			SIM_EXPECT(testDelivered[id] == 1);
			okCount++;
		}
	}

	SIM_EXPECT(okCount > 60);
	SIM_EXPECT(sawDown);
	SIM_EXPECT(testRestarts == 0);
}


int main() {
	TestOutagePausesRetriesAndResendsOnUp();
	TestPacketSentWhileDownIsDeliveredOnceOnUp();
	RunNotWantedAfterOutage<WantingOutChain, VUB::ReliableOf<40, 4>>();
	RunNotWantedAfterOutage<WantingInOrderChain, VUB::ReliableOf<40, 4, true>>();
	TestRandomOutagesKeepExactlyOnce();
	return SimFinish("TestVubReliableLink");
}
