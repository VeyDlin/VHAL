#include "SimCheck.h"
#include "ReliableChains.h"
#include <Drivers/Interface/VUB/Drops.h>
#include <Drivers/Interface/VUB/Meta.h>
#include <Drivers/Interface/VUB/Outbox.h>
#include <Drivers/Interface/VUB/Stack.h>
#include <Drivers/Interface/VUB/TestKit/Loopback.h>
#include <Utilities/DataTypes.h>
#include <cstddef>
#include <optional>
#include <span>

// The health counters (C6): Reliable's resends and peer restarts (Counted), and the stack's high-water marks (HighWater), given to a layer that
// declares usesCounters. The layer reads them in its Process, ahead of the layers below it in the same call. The expectations are worked out
// from the design: the timers of Reliable (a timeout of 200 ms, 3 retries) and the queues of the stack.


// A layer that declares usesCounters and keeps the counters of its last Process.
struct ProbeLayer {
	static constexpr std::size_t header = 0;
	static constexpr uint32 fingerprint = 0x50524F42;
	static constexpr bool usesTime = false;
	static constexpr bool makesBulk = false;
	static constexpr bool usesCounters = true;


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


	void Process(uint32, const VUB::CountingOutbox& outbox) {
		seen = outbox.Counters();
	}


	VUB::StackCounters seen;
};


// A packet chain with the probe and no Reliable, with the high-water marks.
using MarkChain = VUB::Stack<+[] {
	return VUB::Builder()
		.Layer<ProbeLayer>()
		.Link<PacketEnd>()
		.MaxPayload(8)
		.ReceiveQueue(4)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>()
		.HighWater();
}>;


// The probe above a Reliable that is Counted, over a reporting packet line. No high-water marks.
using CountChain = VUB::Stack<+[] {
	return VUB::Builder()
		.Layer<ProbeLayer>()
		.Layer<ReliableLayer>().Retries(3).TimeoutMs(200).Window(4).PayloadBytes(40).Counted()
		.Link<ReportingPacketEnd>()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.OnReceive<&ReliableReceive>()
		.Clock<&TestClock>();
}>;


// Three packets: the control queue holds all three ahead of the first Process, and the far stack's receive queue holds the same three
// until its Process. Each high-water mark is the highest fill of its queue, so it reads 3 on both sides.
void TestHighWaterMarksOfTheQueues() {
	ResetTestState();
	PacketLine line(VUB::Faults{});
	MarkChain near(line.a);
	MarkChain far(line.b);
	std::array<uint8, 2> packet = {1, 2};
	for (uint8 index = 0; index < 3; index++) {
		packet[0] = index;
		SIM_EXPECT(near.Send(std::span<const uint8>(packet.data(), packet.size())) == ResultStatus::ok);
	}

	near.Process();
	SIM_EXPECT(near.Find<ProbeLayer>().seen.highWaterControl == 3);
	SIM_EXPECT(near.Find<ProbeLayer>().seen.highWaterRing == 0);
	line.DeliverToB(far);
	far.Process();
	SIM_EXPECT(far.Find<ProbeLayer>().seen.highWaterRing == 3);
	SIM_EXPECT(far.Find<ProbeLayer>().seen.highWaterControl == 0);
	SIM_EXPECT(far.Find<ProbeLayer>().seen.drops.ringFull == 0);
	SIM_EXPECT(far.Find<ProbeLayer>().seen.drops.queueFull == 0);
	SIM_EXPECT(near.Find<ProbeLayer>().seen.resends == 0);
	SIM_EXPECT(near.Find<ProbeLayer>().seen.peerRestarts == 0);
}


// A sync that is never answered is resent at 200, 400 and 600 ms (three retries), and dropped at 800 ms, which is not a resend. Three
// resends are counted. The probe reads the counts in the Process after the one that counted them.
void TestResendsAreCounted() {
	ResetReliableState();
	testNowUs = 0;
	ReportingPacketLine line(VUB::Faults{});
	CountChain near(line.a);
	near.Find<ReliableLayer>().SetSession(ReliableSession);
	// The first send starts the session: it is busy, and its sync goes out at the next Process.
	SIM_EXPECT(ReliableSend(near, 0) == ResultStatus::busy);
	near.Process();
	testNowUs = 200000;
	near.Process();
	testNowUs = 400000;
	near.Process();
	testNowUs = 600000;
	near.Process();
	testNowUs = 800000;
	near.Process();
	near.Process();
	VUB::StackCounters seen = near.Find<ProbeLayer>().seen;
	SIM_EXPECT(seen.resends == 3);
	SIM_EXPECT(seen.peerRestarts == 0);
	SIM_EXPECT(seen.highWaterRing == 0);
	SIM_EXPECT(seen.highWaterControl == 0);
	SIM_EXPECT(seen.drops.sendError == 0);
}


// One round of the reporting packet line, as the delivery tests run it.
template<typename Near, typename Far>
void Round(ReportingPacketLine& line, Near& near, Far& far) {
	near.Process();
	line.DeliverToB(far);
	far.Process();
	line.DeliverToA(near);
	near.Process();
}


// Sends the payload of id as soon as the layer takes it, running rounds between the tries. Returns true when it was taken.
template<typename Near, typename Far>
bool SendWhenReady(ReportingPacketLine& line, Near& near, Far& far, uint16 id, std::size_t maxRounds) {
	for (std::size_t round = 0; round < maxRounds; round++) {
		if (ReliableSend(near, id) == ResultStatus::ok) {
			return true;
		}

		Round(line, near, far);
	}

	return false;
}


// The far stack is rebuilt in the middle of a run, as the restart tests do. The restart is reported on the near stack once, and the probe
// counts it.
void TestPeerRestartsAreCounted() {
	ResetReliableState();
	ReportingPacketLine line(VUB::Faults{});
	std::optional<CountChain> farStack;
	farStack.emplace(line.b);
	CountChain near(line.a);
	near.Find<ReliableLayer>().SetSession(ReliableSession);
	near.Process();
	farStack->Process();
	for (uint16 id = 0; id < 3; id++) {
		SIM_EXPECT(SendWhenReady(line, near, *farStack, id, 4));
		for (std::size_t round = 0; round < 3; round++) {
			Round(line, near, *farStack);
		}
	}

	farStack.reset();
	farStack.emplace(line.b);
	farStack->Process();
	SIM_EXPECT(ReliableSend(near, 3) == ResultStatus::ok);
	for (std::size_t round = 0; round < 6; round++) {
		Round(line, near, *farStack);
	}

	SIM_EXPECT(SendWhenReady(line, near, *farStack, 4, 6));
	for (std::size_t round = 0; round < 6; round++) {
		Round(line, near, *farStack);
	}

	near.Process();
	SIM_EXPECT(near.Find<ProbeLayer>().seen.peerRestarts == 1);
}


int main() {
	TestHighWaterMarksOfTheQueues();
	TestResendsAreCounted();
	TestPeerRestartsAreCounted();
	return SimFinish("TestVubStackCounters");
}
