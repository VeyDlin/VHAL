#include "Chains.h"
#include "ReliableChains.h"
#include "SimCheck.h"
#include <Drivers/Interface/VUB/Layer/Reliable.h>
#include <Drivers/Interface/VUB/Meta.h>
#include <Drivers/Interface/VUB/Outbox.h>
#include <Drivers/Interface/VUB/Stack.h>
#include <Drivers/Interface/VUB/TestKit/Loopback.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/Result.h>
#include <Utilities/Result/ResultStatus.h>
#include <array>
#include <cstddef>
#include <span>

// The bulk packets of a layer's Next: a packet that the blocks below the producer refuse as busy waits in the stack and is offered again
// ahead of the producer being asked for a new one. A refusal that is not busy is counted as layerError and the packet is dropped.
// Every expected count is worked out by hand from that rule and from the Down policies.


// The producer's counters. Next gives one packet per call, of one byte: the value is the number of the packet, from 0.
inline uint32 bulkProduced = 0;
inline uint32 bulkLimit = 0;


// The refusals the refusing layer gives its next packets: busy first, then errors.
inline uint32 bulkBusyLeft = 0;
inline uint32 bulkErrorLeft = 0;


// The payloads the far side received, in order, and how many; the outcomes of the near side's Reliable layer.
inline std::array<uint8, 16> bulkValues{};
inline std::size_t bulkReceived = 0;
inline std::size_t bulkOutcomesOk = 0;
inline std::size_t bulkOutcomesFailed = 0;


// A layer that makes one-byte bulk packets, up to bulkLimit of them.
struct BulkProducer {
	static constexpr std::size_t header = 0;
	static constexpr uint32 fingerprint = 0x42554C31;
	static constexpr bool usesTime = false;
	static constexpr bool makesBulk = true;


	Result<std::size_t> Add(const VUB::Meta&, std::span<const uint8> in, std::span<uint8> out) {
		if (out.size() < in.size()) {
			return ResultStatus::invalidParameter;
		}

		for (std::size_t index = 0; index < in.size(); index++) {
			out[index] = in[index];
		}

		return in.size();
	}


	Result<VUB::Taken> Take(const VUB::Meta&, std::span<const uint8> packet) {
		(void)packet;
		return VUB::Taken{0, true};
	}


	void Process(uint32, const VUB::Outbox&) {
	}


	Result<std::size_t> Next(VUB::Meta&, std::span<uint8> packet) {
		if (bulkProduced >= bulkLimit) {
			return ResultStatus::noDataAvailable;
		}

		if (packet.size() < 1) {
			return ResultStatus::invalidParameter;
		}

		packet[0] = static_cast<uint8>(bulkProduced);
		bulkProduced++;
		return 1;
	}
};


// A layer below the producer that refuses packets: busy while bulkBusyLeft lasts, then with an error while bulkErrorLeft lasts. The packets it
// takes are passed down unchanged.
struct BulkRefusingLayer {
	static constexpr std::size_t header = 0;
	static constexpr uint32 fingerprint = 0x42554C32;
	static constexpr bool usesTime = false;
	static constexpr bool makesBulk = false;


	Result<std::size_t> Add(const VUB::Meta&, std::span<const uint8> in, std::span<uint8> out) {
		if (bulkBusyLeft > 0) {
			bulkBusyLeft--;
			return ResultStatus::busy;
		}

		if (bulkErrorLeft > 0) {
			bulkErrorLeft--;
			return ResultStatus::invalidParameter;
		}

		if (out.size() < in.size()) {
			return ResultStatus::invalidParameter;
		}

		for (std::size_t index = 0; index < in.size(); index++) {
			out[index] = in[index];
		}

		return in.size();
	}


	Result<VUB::Taken> Take(const VUB::Meta&, std::span<const uint8> packet) {
		(void)packet;
		return VUB::Taken{0, true};
	}


	void Process(uint32, const VUB::Outbox&) {
	}
};


// A packet link that takes every packet and keeps the first byte of each.
class BulkCapture {
public:
	static constexpr VUB::LinkTraits traits = VUB::LinkTraits().Framed().MaxPacket(64).FullDuplex();


	ResultStatus Send(std::span<const uint8> packet) {
		if (count < kinds.size()) {
			kinds[count] = packet.size() > 0 ? packet[0] : 0xFF;
		}

		count++;
		return ResultStatus::ok;
	}


	std::array<uint8, 16> kinds{};
	std::size_t count = 0;
};


// The chain of the producer over the refusing layer over the link, with drop counters.
using BulkRefusingChain = VUB::Stack<+[] {
	return VUB::Builder()
		.CountDrops()
		.Layer<BulkProducer>()
		.Layer<BulkRefusingLayer>()
		.Link<BulkCapture>()
		.MaxPayload(40)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


// Runs the chain for enough calls of Process for every packet to go out.
template<typename StackType>
void RunProcesses(StackType& stack, uint32 calls) {
	for (uint32 step = 0; step < calls; step++) {
		testNowUs += 100;
		stack.Process();
	}
}


// A packet link with Ready() that reports its state to its stack. The packets go to the wire of a line; the test sets the state, and the
// stack must not send while the link is reported down (sendsWhileDown counts the attempts that it does).
class BulkReadyEnd {
public:
	static constexpr VUB::LinkTraits traits = VUB::LinkTraits().Framed().MaxPacket(64).FullDuplex().ReportsState();


	explicit BulkReadyEnd(PacketEnd& target) : inner(&target) {
	}


	bool Ready() const {
		return true;
	}


	void SetStateSink(VUB::StateSink value) {
		sink = value;
		sink.Report(up);
	}


	// Sets the state the link reports to its stack.
	void SetUp(bool value) {
		up = value;
		sink.Report(up);
	}


	ResultStatus Send(std::span<const uint8> packet) {
		if (!up) {
			sendsWhileDown++;
		}

		return inner->Send(packet);
	}


	std::size_t sendsWhileDown = 0;


private:
	PacketEnd* inner;
	VUB::StateSink sink;
	bool up = true;
};


void BulkOutcome(const VUB::Outcome& outcome) {
	if (outcome.status == ResultStatus::ok) {
		bulkOutcomesOk++;
	} else {
		bulkOutcomesFailed++;
	}
}


void BulkReceive(const VUB::Meta&, std::span<const uint8> payload) {
	if (bulkReceived < bulkValues.size() && payload.size() > 0) {
		bulkValues[bulkReceived] = payload[0];
	}

	bulkReceived++;
}


// The near stacks: the producer over Reliable with a window of one or of four, under Down::Wait or Down::Fail.
using BulkNearWait1 = VUB::Stack<+[] {
	return VUB::Builder()
		.CountDrops()
		.Layer<BulkProducer>()
		.Layer<ReliableLayer>().Retries(3).TimeoutMs(200).Window(1).PayloadBytes(40).OnOutcome<&BulkOutcome>().OnRestart<&ReliableRestart>()
		.Link<BulkReadyEnd>()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.OnReceive<&BulkReceive>()
		.Clock<&TestClock>()
		.WhileDown(VUB::Down::Wait);
}>;


using BulkNearWait4 = VUB::Stack<+[] {
	return VUB::Builder()
		.CountDrops()
		.Layer<BulkProducer>()
		.Layer<ReliableLayer>().Retries(3).TimeoutMs(200).Window(4).PayloadBytes(40).OnOutcome<&BulkOutcome>().OnRestart<&ReliableRestart>()
		.Link<BulkReadyEnd>()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.OnReceive<&BulkReceive>()
		.Clock<&TestClock>()
		.WhileDown(VUB::Down::Wait);
}>;


using BulkNearFail1 = VUB::Stack<+[] {
	return VUB::Builder()
		.CountDrops()
		.Layer<BulkProducer>()
		.Layer<ReliableLayer>().Retries(3).TimeoutMs(200).Window(1).PayloadBytes(40).OnOutcome<&BulkOutcome>().OnRestart<&ReliableRestart>()
		.Link<BulkReadyEnd>()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.OnReceive<&BulkReceive>()
		.Clock<&TestClock>()
		.WhileDown(VUB::Down::Fail);
}>;


// The far stack: a Reliable layer with a window of four, which only receives.
using BulkFar = VUB::Stack<+[] {
	return VUB::Builder()
		.Layer<ReliableLayer>().Retries(3).TimeoutMs(200).Window(4).PayloadBytes(40).OnOutcome<&BulkOutcome>().OnRestart<&ReliableRestart>()
		.Link<PacketEnd>()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.OnReceive<&BulkReceive>()
		.Clock<&TestClock>();
}>;


// One round of the line: the near stack, the far stack, the near stack again. The acknowledgements of the far side come back in the round.
template<typename NearType>
void BulkRound(PacketLine& line, NearType& near, BulkFar& far) {
	testNowUs += 1000;
	near.Process();
	line.DeliverToB(far);
	far.Process();
	line.DeliverToA(near);
	near.Process();
}


// Clears the counters of the bulk test, after a warm-up whose packet is not part of the test.
void ClearBulkCounters() {
	bulkProduced = 0;
	bulkLimit = 0;
	bulkBusyLeft = 0;
	bulkErrorLeft = 0;
	bulkValues.fill(0);
	bulkReceived = 0;
	bulkOutcomesOk = 0;
	bulkOutcomesFailed = 0;
}


// Starts the session of both sides with a warm-up packet of the application, and runs until it is delivered.
template<typename NearType>
void BulkSessionUp(PacketLine& line, NearType& near, BulkFar& far) {
	testNowUs = 0;
	near.Process();
	far.Process();
	std::size_t tries = 0;
	while (ReliableSend(near, 0) != ResultStatus::ok && tries < 100) {
		BulkRound(line, near, far);
		tries++;
	}

	for (uint32 step = 0; step < 200; step++) {
		BulkRound(line, near, far);
	}

	ClearBulkCounters();
}


// A layer's busy refusal of the packet below it is not a loss: the packet waits and is sent by a following call, in order, and nothing is
// counted. Three packets, the first refused once.
void TestBusyBulkPacketIsKept() {
	ResetTestState();
	ClearBulkCounters();
	bulkLimit = 3;
	bulkBusyLeft = 1;
	BulkCapture link;
	BulkRefusingChain stack(link);
	RunProcesses(stack, 12);
	SIM_EXPECT(bulkProduced == 3);
	SIM_EXPECT(link.count == 3);
	SIM_EXPECT(link.kinds[0] == 0);
	SIM_EXPECT(link.kinds[1] == 1);
	SIM_EXPECT(link.kinds[2] == 2);
	SIM_EXPECT(stack.Drops().layerError == 0);
	SIM_EXPECT(stack.Drops().linkDown == 0);
}


// A refusal that is not busy is counted as layerError, and that packet is dropped. Three packets, the first refused with an error: the other
// two go out, and layerError is 1.
void TestErrorBulkPacketIsCounted() {
	ResetTestState();
	ClearBulkCounters();
	bulkLimit = 3;
	bulkErrorLeft = 1;
	BulkCapture link;
	BulkRefusingChain stack(link);
	RunProcesses(stack, 12);
	SIM_EXPECT(bulkProduced == 3);
	SIM_EXPECT(link.count == 2);
	SIM_EXPECT(link.kinds[0] == 1);
	SIM_EXPECT(link.kinds[1] == 2);
	SIM_EXPECT(stack.Drops().layerError == 1);
	SIM_EXPECT(stack.Drops().linkDown == 0);
}


// Window 1, the link up, six packets: each goes out when the one ahead of it is acknowledged, so the window refuses the producer's packets as
// busy in between. All six arrive in order; nothing is counted.
void TestWindow1DeliversAll() {
	ResetReliableState();
	PacketLine line(VUB::Faults{});
	BulkReadyEnd nearEnd(line.a);
	BulkNearWait1 near(nearEnd);
	GiveSession(near);
	BulkFar far(line.b);
	GiveSession(far);
	BulkSessionUp(line, near, far);
	bulkLimit = 6;
	for (uint32 step = 0; step < 400; step++) {
		BulkRound(line, near, far);
	}

	SIM_EXPECT(bulkProduced == 6);
	SIM_EXPECT(bulkReceived == 6);
	for (std::size_t index = 0; index < 6; index++) {
		SIM_EXPECT(static_cast<std::size_t>(bulkValues[index]) == index);
	}

	SIM_EXPECT(bulkOutcomesOk == 6);
	SIM_EXPECT(bulkOutcomesFailed == 0);
	SIM_EXPECT(near.Drops().layerError == 0);
	SIM_EXPECT(near.Drops().linkDown == 0);
	SIM_EXPECT(nearEnd.sendsWhileDown == 0);
}


// Window 4, the link up, six packets: the window takes four in flight. All six arrive in order; nothing is counted.
void TestWindow4DeliversAll() {
	ResetReliableState();
	PacketLine line(VUB::Faults{});
	BulkReadyEnd nearEnd(line.a);
	BulkNearWait4 near(nearEnd);
	GiveSession(near);
	BulkFar far(line.b);
	GiveSession(far);
	BulkSessionUp(line, near, far);
	bulkLimit = 6;
	for (uint32 step = 0; step < 400; step++) {
		BulkRound(line, near, far);
	}

	SIM_EXPECT(bulkProduced == 6);
	SIM_EXPECT(bulkReceived == 6);
	for (std::size_t index = 0; index < 6; index++) {
		SIM_EXPECT(static_cast<std::size_t>(bulkValues[index]) == index);
	}

	SIM_EXPECT(bulkOutcomesOk == 6);
	SIM_EXPECT(bulkOutcomesFailed == 0);
	SIM_EXPECT(near.Drops().layerError == 0);
	SIM_EXPECT(near.Drops().linkDown == 0);
	SIM_EXPECT(nearEnd.sendsWhileDown == 0);
}


// The session is not started when the bulk data begins: the first packets are busy until the session is up, and they wait. Six packets
// arrive in order; nothing is counted.
void TestSessionStartingDeliversAll() {
	ResetReliableState();
	ClearBulkCounters();
	PacketLine line(VUB::Faults{});
	BulkReadyEnd nearEnd(line.a);
	BulkNearWait1 near(nearEnd);
	GiveSession(near);
	BulkFar far(line.b);
	GiveSession(far);
	testNowUs = 0;
	near.Process();
	far.Process();
	bulkLimit = 6;
	for (uint32 step = 0; step < 400; step++) {
		BulkRound(line, near, far);
	}

	SIM_EXPECT(bulkProduced == 6);
	SIM_EXPECT(bulkReceived == 6);
	for (std::size_t index = 0; index < 6; index++) {
		SIM_EXPECT(static_cast<std::size_t>(bulkValues[index]) == index);
	}

	SIM_EXPECT(bulkOutcomesOk == 6);
	SIM_EXPECT(bulkOutcomesFailed == 0);
	SIM_EXPECT(near.Drops().layerError == 0);
	SIM_EXPECT(near.Drops().linkDown == 0);
	SIM_EXPECT(nearEnd.sendsWhileDown == 0);
}


// The window of one is full with packet 0, which is not delivered in the first two rounds. Packet 1 is refused as busy by the window and
// waits in the stack. The link is reported down ahead of the next Process, and up again after it.
template<typename NearType>
void FallWhileWaiting(PacketLine& line, NearType& near, BulkFar& far, BulkReadyEnd& nearEnd) {
	bulkLimit = 3;
	testNowUs += 1000;
	near.Process();
	testNowUs += 1000;
	near.Process();
	nearEnd.SetUp(false);
	testNowUs += 1000;
	near.Process();
	nearEnd.SetUp(true);
	for (uint32 step = 0; step < 400; step++) {
		BulkRound(line, near, far);
	}
}


// Down::Wait: the packet that waits stays in the stack through the outage and goes out when the link is up. Three packets arrive in order;
// nothing is dropped.
void TestLinkDownWaitKeepsBulk() {
	ResetReliableState();
	PacketLine line(VUB::Faults{});
	BulkReadyEnd nearEnd(line.a);
	BulkNearWait1 near(nearEnd);
	GiveSession(near);
	BulkFar far(line.b);
	GiveSession(far);
	BulkSessionUp(line, near, far);
	FallWhileWaiting(line, near, far, nearEnd);
	SIM_EXPECT(bulkProduced == 3);
	SIM_EXPECT(bulkReceived == 3);
	for (std::size_t index = 0; index < 3; index++) {
		SIM_EXPECT(static_cast<std::size_t>(bulkValues[index]) == index);
	}

	SIM_EXPECT(bulkOutcomesOk == 3);
	SIM_EXPECT(bulkOutcomesFailed == 0);
	SIM_EXPECT(near.Drops().layerError == 0);
	SIM_EXPECT(near.Drops().linkDown == 0);
	SIM_EXPECT(nearEnd.sendsWhileDown == 0);
}


// Down::Fail: the packet that waits is dropped when the link falls, and counted as linkDown. Packets 0 and 2 arrive, in order; packet 1 is
// the one dropped.
void TestLinkDownFailDropsBulk() {
	ResetReliableState();
	PacketLine line(VUB::Faults{});
	BulkReadyEnd nearEnd(line.a);
	BulkNearFail1 near(nearEnd);
	GiveSession(near);
	BulkFar far(line.b);
	GiveSession(far);
	BulkSessionUp(line, near, far);
	FallWhileWaiting(line, near, far, nearEnd);
	SIM_EXPECT(bulkProduced == 3);
	SIM_EXPECT(bulkReceived == 2);
	SIM_EXPECT(bulkValues[0] == 0);
	SIM_EXPECT(bulkValues[1] == 2);
	SIM_EXPECT(bulkOutcomesOk == 2);
	SIM_EXPECT(bulkOutcomesFailed == 0);
	SIM_EXPECT(near.Drops().layerError == 0);
	SIM_EXPECT(near.Drops().linkDown == 1);
	SIM_EXPECT(nearEnd.sendsWhileDown == 0);
}


int main() {
	TestBusyBulkPacketIsKept();
	TestErrorBulkPacketIsCounted();
	TestWindow1DeliversAll();
	TestWindow4DeliversAll();
	TestSessionStartingDeliversAll();
	TestLinkDownWaitKeepsBulk();
	TestLinkDownFailDropsBulk();
	return SimFinish("TestVubStackBulk");
}
