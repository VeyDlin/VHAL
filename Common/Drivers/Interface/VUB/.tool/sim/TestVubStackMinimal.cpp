#include "Chains.h"
#include "SimCheck.h"
#include <Drivers/Interface/VUB/Drops.h>
#include <Drivers/Interface/VUB/Framing/Cobs.h>
#include <Drivers/Interface/VUB/Framing/Gap.h>
#include <Drivers/Interface/VUB/LinkTraits.h>
#include <Drivers/Interface/VUB/Meta.h>
#include <Drivers/Interface/VUB/Outbox.h>
#include <Drivers/Interface/VUB/Stack.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/Result.h>
#include <Utilities/Result/ResultStatus.h>
#include <array>
#include <cstddef>
#include <span>
#include <utility>

// The stack with no send queue, no drop counters and no staging ring: the parts a chain does not ask for are not built. Each check is
// worked out by hand from the rules of the parts (COBS, the gap, the Down options) and from the send path described in Stack.h.


// A byte-stream link that takes up to the budget of the round and keeps the bytes it took. With Reports it reports its state to its stack.
template<bool Reports>
class MinStreamOf {
public:
	static constexpr VUB::LinkTraits traits = Reports
		? VUB::LinkTraits().MaxPacket(64).HalfDuplex().ReportsState()
		: VUB::LinkTraits().MaxPacket(64).HalfDuplex();


	void StartRound(std::size_t bytes) {
		budget = bytes;
	}


	Result<std::size_t> Write(std::span<const uint8> bytes) {
		std::size_t accepted = bytes.size() < budget ? bytes.size() : budget;
		for (std::size_t index = 0; index < accepted; index++) {
			if (count < wire.size()) {
				wire[count] = bytes[index];
			}

			count++;
		}

		budget -= accepted;
		return accepted;
	}


	void SetStateSink(VUB::StateSink value) requires (Reports) {
		sink = value;
		sink.Report(up);
	}


	// Sets the state the link reports to its stack.
	void SetUp(bool value) requires (Reports) {
		up = value;
		sink.Report(up);
	}


	std::array<uint8, 256> wire{};
	std::size_t count = 0;
	std::size_t budget = 1000;


private:
	VUB::StateSink sink;
	bool up = true;
};


using MinStream = MinStreamOf<false>;
using MinReportingStream = MinStreamOf<true>;


// A packet link that says whether its next Send is taken. Send takes one packet per budget unit and returns busy when there is none.
class MinReadyPacket {
public:
	static constexpr VUB::LinkTraits traits = VUB::LinkTraits().Framed().MaxPacket(64).FullDuplex();


	bool Ready() const {
		return budget > 0;
	}


	ResultStatus Send(std::span<const uint8> packet) {
		if (budget == 0) {
			return ResultStatus::busy;
		}

		budget--;
		packets++;
		lastLength = packet.size();
		return ResultStatus::ok;
	}


	std::size_t budget = 1000;
	std::size_t packets = 0;
	std::size_t lastLength = 0;
};


// A packet link with no Ready(): a packet the link refuses is refused after the layers have run on it.
class MinPlainPacket {
public:
	static constexpr VUB::LinkTraits traits = VUB::LinkTraits().Framed().MaxPacket(64).FullDuplex();


	ResultStatus Send(std::span<const uint8> packet) {
		if (budget == 0) {
			return ResultStatus::busy;
		}

		budget--;
		packets++;
		lastLength = packet.size();
		return ResultStatus::ok;
	}


	std::size_t budget = 1000;
	std::size_t packets = 0;
	std::size_t lastLength = 0;
};


// The packets the counting layer was asked to add.
inline uint32 minCountingAdds = 0;


// A layer that counts the packets it is asked to add, and passes them down unchanged.
struct MinCountingLayer {
	static constexpr std::size_t header = 0;
	static constexpr uint32 fingerprint = 0x4D494E31;
	static constexpr bool usesTime = false;
	static constexpr bool makesBulk = false;


	Result<std::size_t> Add(const VUB::Meta&, std::span<const uint8> in, std::span<uint8> out) {
		minCountingAdds++;
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


// A lowest layer that declares its peer. Its first Process takes the peer down; its second Process sends one packet of its own.
struct MinPeerLayer {
	static constexpr std::size_t header = 0;
	static constexpr uint32 fingerprint = 0x4D505231;
	static constexpr bool usesTime = false;
	static constexpr bool makesBulk = false;
	static constexpr bool reportsPeer = true;


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
		if (steps == 0) {
			outbox.SetPeer(false);
		} else if (steps == 1) {
			std::array<uint8, 1> own{9};
			sendResult = outbox.Send(std::span<const uint8>(own.data(), own.size()));
		}

		steps++;
	}


	uint32 steps = 0;
	ResultStatus sendResult = ResultStatus::notSupported;
};


// A lowest layer that sends one packet of its own from its first Process, with no peer declared.
struct MinSendLayer {
	static constexpr std::size_t header = 0;
	static constexpr uint32 fingerprint = 0x4D534C31;
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
		if (!sent) {
			std::array<uint8, 2> own{7, 8};
			result = outbox.Send(std::span<const uint8>(own.data(), own.size()));
			sent = true;
		}
	}


	bool sent = false;
	ResultStatus result = ResultStatus::notSupported;
};


// Byte-stream chains over COBS, with no send queue and no counters.
using MinStreamStack = VUB::Stack<+[] {
	return VUB::Builder()
		.Framing<VUB::Cobs>()
		.Link<MinStream>()
		.MaxPayload(16)
		.OnReceive<&TestReceive>();
}>;


// The same chain with the parts that were built by default before: a send queue of 4 and the drop counters.
using MinFullStack = VUB::Stack<+[] {
	return VUB::Builder()
		.Framing<VUB::Cobs>()
		.Link<MinStream>()
		.MaxPayload(16)
		.OnReceive<&TestReceive>()
		.SendQueue(4)
		.CountDrops();
}>;


// The counting layer above COBS over a stream link. Its send queue is the default, which is zero.
using MinCountedStack = VUB::Stack<+[] {
	return VUB::Builder()
		.Layer<MinCountingLayer>()
		.Framing<VUB::Cobs>()
		.Link<MinStream>()
		.MaxPayload(16)
		.OnReceive<&TestReceive>();
}>;


// The counting layer above a packet link with Ready(): the default send queue is zero.
using MinReadyCountedStack = VUB::Stack<+[] {
	return VUB::Builder()
		.Layer<MinCountingLayer>()
		.Link<MinReadyPacket>()
		.MaxPayload(16)
		.ReceiveQueue(4)
		.OnReceive<&TestReceive>();
}>;


// A packet link with no Ready(): the default send queue is one packet.
using MinPlainPacketStack = VUB::Stack<+[] {
	return VUB::Builder()
		.Link<MinPlainPacket>()
		.MaxPayload(16)
		.ReceiveQueue(4)
		.OnReceive<&TestReceive>();
}>;


// The gap framing on a stream link with a clock: the gap after the last byte keeps the next frame back.
using MinGapStack = VUB::Stack<+[] {
	return VUB::Builder()
		.Framing<VUB::Gap>()
		.Link<MinStream>()
		.MaxPayload(16)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


// The peer-declaring layer over COBS, with no send queue.
using MinPeerStack = VUB::Stack<+[] {
	return VUB::Builder()
		.Layer<MinPeerLayer>()
		.Framing<VUB::Cobs>()
		.Link<MinStream>()
		.MaxPayload(16)
		.OnReceive<&TestReceive>();
}>;


// A reporting stream link under the two Down policies.
using MinWaitStack = VUB::Stack<+[] {
	return VUB::Builder()
		.Framing<VUB::Cobs>()
		.Link<MinReportingStream>()
		.MaxPayload(16)
		.OnReceive<&TestReceive>()
		.WhileDown(VUB::Down::Wait);
}>;


using MinFailStack = VUB::Stack<+[] {
	return VUB::Builder()
		.Framing<VUB::Cobs>()
		.Link<MinReportingStream>()
		.MaxPayload(16)
		.OnReceive<&TestReceive>()
		.WhileDown(VUB::Down::Fail);
}>;


// A layer that sends from its Process on a stream chain with no send queue.
using MinSendLayerStack = VUB::Stack<+[] {
	return VUB::Builder()
		.Layer<MinSendLayer>()
		.Framing<VUB::Cobs>()
		.Link<MinStream>()
		.MaxPayload(16)
		.OnReceive<&TestReceive>();
}>;


// A bulk layer on a stream chain. The chain has no SendQueue call: a chain with a bulk layer always has a send queue, of one packet by default.
using MinBulkDefaultStack = VUB::Stack<+[] {
	return VUB::Builder()
		.Layer<TestLayer>(TestLayerOptions{0, true, 2})
		.Framing<VUB::Cobs>()
		.Link<MinStream>()
		.MaxPayload(16)
		.ReceiveRing(256)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


// Receive chains: a receive buffer of the payload, larger than the payload, and the drop counters to see a frame that is too long.
using MinReceiveStack = VUB::Stack<+[] {
	return VUB::Builder()
		.Framing<VUB::Cobs>()
		.Link<MinStream>()
		.MaxPayload(16)
		.CountDrops()
		.OnReceive<&TestReceive>();
}>;


using MinReceiveLargerStack = VUB::Stack<+[] {
	return VUB::Builder()
		.Framing<VUB::Cobs>()
		.Link<MinStream>()
		.MaxPayload(16)
		.MaxReceive(40)
		.CountDrops()
		.OnReceive<&TestReceive>();
}>;


// A concurrent stack with no staging ring: it is built, and its layers get the flag.
using MinConcurrentStack = VUB::Stack<+[] {
	return VUB::Builder()
		.Framing<VUB::Cobs>()
		.Link<MinStream>()
		.MaxPayload(16)
		.Concurrent()
		.OnReceive<&TestReceive>();
}>;


using MinStagedStack = VUB::Stack<+[] {
	return VUB::Builder()
		.Framing<VUB::Cobs>()
		.Link<MinStream>()
		.MaxPayload(16)
		.Concurrent()
		.StageQueue(4)
		.OnReceive<&TestReceive>();
}>;


// Whether a stack has a Drops member function.
template<typename StackType>
concept HasDrops = requires(const StackType& stack) {
	stack.Drops();
};


// Whether the stack type is still the empty-parts chain: its Drops is not a member.
static_assert(!HasDrops<MinStreamStack>, "Drops() is a member without CountDrops()");
static_assert(HasDrops<MinFullStack>, "Drops() is not a member with CountDrops()");


// Feeds a COBS frame of a payload of nonzero bytes into a stack: the length code, the bytes, and the zero that ends the frame.
template<typename StackType>
void FeedFrame(StackType& stack, uint8 length) {
	stack.Feed(static_cast<uint8>(length + 1));
	for (uint8 index = 0; index < length; index++) {
		stack.Feed(static_cast<uint8>(index + 1));
	}

	stack.Feed(0);
}


// Send on a stream chain with no send queue writes the frame in the call, and Process has nothing left to write.
void TestStreamSendWritesAtOnce() {
	ResetTestState();
	MinStream link;
	MinStreamStack stack(link);
	const std::array<uint8, 3> payload = {1, 2, 3};
	SIM_EXPECT(stack.Send(payload) == ResultStatus::ok);
	const std::array<uint8, 5> expected = {0x04, 0x01, 0x02, 0x03, 0x00};
	SIM_EXPECT(link.count == expected.size());
	for (std::size_t index = 0; index < expected.size(); index++) {
		SIM_EXPECT(link.wire[index] == expected[index]);
	}

	stack.Process();
	SIM_EXPECT(link.count == expected.size());
}


// When the link takes part of a framed packet, the packet is not complete: Send returns busy, takes nothing, and Process writes the rest.
void TestStreamBusyWhilePartWritten() {
	ResetTestState();
	MinStream link;
	MinStreamStack stack(link);
	link.StartRound(2);
	const std::array<uint8, 3> first = {1, 2, 3};
	SIM_EXPECT(stack.Send(first) == ResultStatus::ok);
	SIM_EXPECT(link.count == 2);

	const std::array<uint8, 2> second = {5, 6};
	SIM_EXPECT(stack.Send(second) == ResultStatus::busy);
	SIM_EXPECT(link.count == 2);

	link.StartRound(100);
	stack.Process();
	SIM_EXPECT(link.count == 5);
	const std::array<uint8, 5> expected = {0x04, 0x01, 0x02, 0x03, 0x00};
	for (std::size_t index = 0; index < expected.size(); index++) {
		SIM_EXPECT(link.wire[index] == expected[index]);
	}

	// The second packet is written in the call: 03 05 06 00.
	SIM_EXPECT(stack.Send(second) == ResultStatus::ok);
	SIM_EXPECT(link.count == 5 + 4);
	stack.Process();
	SIM_EXPECT(link.count == 5 + 4);
}


// A busy send is refused before the layers run: the counting layer does not add the packet the link then has no room for. The first
// packet is partly written, so the second is refused at once, and the counting layer has added one packet only.
void TestStreamBusyTakesNothing() {
	ResetTestState();
	minCountingAdds = 0;
	MinStream link;
	MinCountedStack stack(link);
	link.StartRound(2);
	const std::array<uint8, 3> first = {1, 2, 3};
	SIM_EXPECT(stack.Send(first) == ResultStatus::ok);
	SIM_EXPECT(link.count == 2);
	SIM_EXPECT(stack.Send(first) == ResultStatus::busy);
	SIM_EXPECT(minCountingAdds == 1);
}


// A packet link with Ready(): the packet goes to the link in the call; Ready false is busy before the layers run.
void TestPacketSendWritesAtOnce() {
	ResetTestState();
	MinReadyPacket link;
	MinReadyCountedStack stack(link);
	link.budget = 1;
	const std::array<uint8, 2> first = {1, 2};
	SIM_EXPECT(stack.Send(first) == ResultStatus::ok);
	SIM_EXPECT(link.packets == 1);
	SIM_EXPECT(link.lastLength == 2);

	SIM_EXPECT(stack.Send(first) == ResultStatus::busy);
	SIM_EXPECT(link.packets == 1);

	link.budget = 5;
	SIM_EXPECT(stack.Send(first) == ResultStatus::ok);
	SIM_EXPECT(link.packets == 2);
}


// A packet link with no Ready(): the default send queue holds one packet. The second packet waits for Process.
void TestPlainPacketDefaultQueueOfOne() {
	ResetTestState();
	MinPlainPacket link;
	MinPlainPacketStack stack(link);
	link.budget = 0;
	const std::array<uint8, 2> first = {1, 2};
	SIM_EXPECT(stack.Send(first) == ResultStatus::ok);
	SIM_EXPECT(stack.Send(first) == ResultStatus::busy);
	SIM_EXPECT(link.packets == 0);

	link.budget = 5;
	stack.Process();
	SIM_EXPECT(link.packets == 1);
}


// The gap framing: a frame may not start until the gap after the last byte has passed.
void TestGapSendWaitsForGap() {
	ResetTestState();
	MinStream link;
	MinGapStack stack(link);
	const std::array<uint8, 2> payload = {1, 2};
	SIM_EXPECT(stack.Send(payload) == ResultStatus::ok);
	SIM_EXPECT(link.count > 0);
	const std::size_t afterFirst = link.count;

	testNowUs = 100;
	SIM_EXPECT(stack.Send(payload) == ResultStatus::busy);
	SIM_EXPECT(link.count == afterFirst);

	testNowUs = 100000;
	SIM_EXPECT(stack.Send(payload) == ResultStatus::ok);
	SIM_EXPECT(link.count > afterFirst);
}


// While only the peer is down, the application's packet waits (busy under Down::Wait), and the layer's own packet goes: its frame of
// the byte 09 is 02 09 00.
void TestPeerDownLayerPacketGoes() {
	ResetTestState();
	MinStream link;
	MinPeerStack stack(link);
	stack.Process();
	SIM_EXPECT(!stack.LinkUp());

	const std::array<uint8, 2> payload = {1, 2};
	SIM_EXPECT(stack.Send(payload) == ResultStatus::busy);
	SIM_EXPECT(link.count == 0);

	stack.Process();
	SIM_EXPECT(link.count == 3);
	const std::array<uint8, 3> expected = {0x02, 0x09, 0x00};
	for (std::size_t index = 0; index < expected.size(); index++) {
		SIM_EXPECT(link.wire[index] == expected[index]);
	}
}


// A layer's own packet from Process on a stream chain with no send queue is written in the call.
void TestLayerOutboxWritesAtOnce() {
	ResetTestState();
	MinStream link;
	MinSendLayerStack stack(link);
	stack.Process();
	SIM_EXPECT(link.count == 4);
	const std::array<uint8, 4> expected = {0x03, 0x07, 0x08, 0x00};
	for (std::size_t index = 0; index < expected.size(); index++) {
		SIM_EXPECT(link.wire[index] == expected[index]);
	}
}


// A bulk chain with no SendQueue call has a send queue of one packet, so the application and the control lane find the link free between
// bulk frames. The chain's bulk packets go out in Process, in order: 02 00 00 BB and 02 01 00 BB, with the sequence numbers of the layer,
// and their COBS frames are 02 02 01 02 BB 00 and 03 02 01 02 BB 00.
void TestBulkChainDefaultsToOneQueuedPacket() {
	ResetTestState();
	SIM_EXPECT(MinBulkDefaultStack::sendQueue == 1);
	MinStream link;
	MinBulkDefaultStack stack(link);
	stack.Process();
	const std::array<uint8, 12> expected = {0x02, 0x02, 0x01, 0x02, 0xBB, 0x00, 0x03, 0x02, 0x01, 0x02, 0xBB, 0x00};
	SIM_EXPECT(link.count == expected.size());
	for (std::size_t index = 0; index < expected.size(); index++) {
		SIM_EXPECT(link.wire[index] == expected[index]);
	}
}


// A concurrent chain with no send queue and a staging ring, over a link that reports its state, under Down::Fail. A packet staged before
// the connection goes down is dropped when Process moves it on, and counted as linkDown; it is not sent after the connection returns.
using MinStagedFailStack = VUB::Stack<+[] {
	return VUB::Builder()
		.CountDrops()
		.Framing<VUB::Cobs>()
		.Link<MinReportingStream>()
		.MaxPayload(16)
		.Concurrent()
		.StageQueue(4)
		.WhileDown(VUB::Down::Fail)
		.OnReceive<&TestReceive>();
}>;


void TestStagedPacketDroppedWhenDownUnderFail() {
	ResetTestState();
	MinReportingStream link;
	MinStagedFailStack stack(link);
	stack.Process();
	const std::array<uint8, 3> payload = {1, 2, 3};
	SIM_EXPECT(stack.Send(payload) == ResultStatus::ok);
	link.SetUp(false);
	stack.Process();
	SIM_EXPECT(stack.Drops().linkDown == 1);
	SIM_EXPECT(link.count == 0);
	link.SetUp(true);
	stack.Process();
	SIM_EXPECT(link.count == 0);
	SIM_EXPECT(stack.Drops().linkDown == 1);
}


// Send and the Down policies: Wait returns busy while the link is down, Fail returns noConnection.
void TestDownWaitIsBusy() {
	ResetTestState();
	MinReportingStream link;
	MinWaitStack stack(link);
	stack.Process();
	SIM_EXPECT(stack.LinkUp());
	link.SetUp(false);
	stack.Process();
	SIM_EXPECT(!stack.LinkUp());
	const std::array<uint8, 2> payload = {1, 2};
	SIM_EXPECT(stack.Send(payload) == ResultStatus::busy);
	SIM_EXPECT(link.count == 0);

	link.SetUp(true);
	stack.Process();
	SIM_EXPECT(stack.LinkUp());
	SIM_EXPECT(stack.Send(payload) == ResultStatus::ok);
}


void TestDownFailIsNoConnection() {
	ResetTestState();
	MinReportingStream link;
	MinFailStack stack(link);
	stack.Process();
	link.SetUp(false);
	stack.Process();
	const std::array<uint8, 2> payload = {1, 2};
	SIM_EXPECT(stack.Send(payload) == ResultStatus::noConnection);
	SIM_EXPECT(link.count == 0);
}


// A chain without the parts is smaller than the chain with them.
void TestSizeWithoutParts() {
	SIM_EXPECT(sizeof(MinStreamStack) < sizeof(MinFullStack));
}


// The receive buffer is the largest packet of the chain: a frame of 16 bytes comes up, and a frame of 17 bytes counts as too long.
void TestReceiveBufferIsMaxPayload() {
	ResetTestState();
	MinStream link;
	MinReceiveStack stack(link);
	FeedFrame(stack, 16);
	stack.Process();
	SIM_EXPECT(testReceivedCount == 1);
	SIM_EXPECT(testReceivedLength[0] == 16);
	SIM_EXPECT(stack.Drops().tooLong == 0);

	FeedFrame(stack, 17);
	stack.Process();
	SIM_EXPECT(testReceivedCount == 1);
	SIM_EXPECT(stack.Drops().tooLong == 1);
}


// MaxReceive larger than MaxPayload: a 40-byte frame comes up, and a 41-byte frame counts as too long.
void TestReceiveLargerThanMaxPayload() {
	ResetTestState();
	MinStream link;
	MinReceiveLargerStack stack(link);
	FeedFrame(stack, 40);
	stack.Process();
	SIM_EXPECT(testReceivedCount == 1);
	SIM_EXPECT(testReceivedLength[0] == 40);

	FeedFrame(stack, 41);
	stack.Process();
	SIM_EXPECT(testReceivedCount == 1);
	SIM_EXPECT(stack.Drops().tooLong == 1);
}


// A concurrent chain with no staging ring is built and runs: its packets are sent from the handle of a layer or from Process.
void TestConcurrentWithoutStagingRing() {
	ResetTestState();
	MinStream link;
	MinConcurrentStack stack(link);
	stack.Process();
	SIM_EXPECT(link.count == 0);
}


// A staged packet is sent by Process, not in the call of Send.
void TestStagedSendWaitsForProcess() {
	ResetTestState();
	MinStream link;
	MinStagedStack stack(link);
	const std::array<uint8, 3> payload = {1, 2, 3};
	SIM_EXPECT(stack.Send(payload) == ResultStatus::ok);
	SIM_EXPECT(link.count == 0);
	stack.Process();
	SIM_EXPECT(link.count == 5);
}


// A packet link with Ready() that reports its state to its stack. Its Send counts the packets it takes, and the attempts made while down.
class MinReportingReadyPacket {
public:
	static constexpr VUB::LinkTraits traits = VUB::LinkTraits().Framed().MaxPacket(64).FullDuplex().ReportsState();


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


	ResultStatus Send(std::span<const uint8>) {
		if (!up) {
			sendsWhileDown++;
		}

		packets++;
		return ResultStatus::ok;
	}


	std::size_t packets = 0;
	std::size_t sendsWhileDown = 0;


private:
	VUB::StateSink sink;
	bool up = true;
};


// A packet link with a reporting state and Ready(): the default send queue is zero.
using MinReadyReportingStack = VUB::Stack<+[] {
	return VUB::Builder()
		.Link<MinReportingReadyPacket>()
		.MaxPayload(16)
		.ReceiveQueue(4)
		.OnReceive<&TestReceive>();
}>;


// A stream chain under Down::Wait with no send queue: a Send made after the link reports its fall, and ahead of the next Process, is busy and
// takes nothing. The same holds after the link reports its return, until the next Process reads the return.
void TestWaitRefusesAfterReportedFall() {
	ResetTestState();
	MinReportingStream link;
	MinWaitStack stack(link);
	stack.Process();
	const std::array<uint8, 2> payload = {1, 2};
	link.SetUp(false);
	SIM_EXPECT(stack.Send(payload) == ResultStatus::busy);
	SIM_EXPECT(link.count == 0);

	link.SetUp(true);
	SIM_EXPECT(stack.Send(payload) == ResultStatus::busy);
	SIM_EXPECT(link.count == 0);

	stack.Process();
	SIM_EXPECT(stack.LinkUp());
	SIM_EXPECT(stack.Send(payload) == ResultStatus::ok);
	SIM_EXPECT(link.count == 4);
}


// The same under Down::Fail: a Send made after the reported fall, and ahead of the next Process, is refused with noConnection and takes nothing.
void TestFailRefusesAfterReportedFall() {
	ResetTestState();
	MinReportingStream link;
	MinFailStack stack(link);
	stack.Process();
	const std::array<uint8, 2> payload = {1, 2};
	link.SetUp(false);
	SIM_EXPECT(stack.Send(payload) == ResultStatus::noConnection);
	SIM_EXPECT(link.count == 0);

	link.SetUp(true);
	SIM_EXPECT(stack.Send(payload) == ResultStatus::noConnection);
	SIM_EXPECT(link.count == 0);

	stack.Process();
	SIM_EXPECT(stack.Send(payload) == ResultStatus::ok);
	SIM_EXPECT(link.count == 4);
}


// A packet link with Ready() that reports its state: the reported fall refuses the packet ahead of any ask of the link, and nothing is sent to
// a link that is down.
void TestPacketReadyRefusesAfterReportedFall() {
	ResetTestState();
	MinReportingReadyPacket link;
	MinReadyReportingStack stack(link);
	stack.Process();
	const std::array<uint8, 2> payload = {1, 2};
	link.SetUp(false);
	SIM_EXPECT(stack.Send(payload) == ResultStatus::busy);
	SIM_EXPECT(link.packets == 0);

	link.SetUp(true);
	SIM_EXPECT(stack.Send(payload) == ResultStatus::busy);
	SIM_EXPECT(link.packets == 0);

	stack.Process();
	SIM_EXPECT(stack.Send(payload) == ResultStatus::ok);
	SIM_EXPECT(link.packets == 1);
	SIM_EXPECT(link.sendsWhileDown == 0);
}


// The critical section of the host build. The tests run in one context, so there is nothing to exclude.
void System::CriticalSection(bool) {
}


int main() {
	TestStreamSendWritesAtOnce();
	TestStreamBusyWhilePartWritten();
	TestStreamBusyTakesNothing();
	TestPacketSendWritesAtOnce();
	TestPlainPacketDefaultQueueOfOne();
	TestGapSendWaitsForGap();
	TestPeerDownLayerPacketGoes();
	TestLayerOutboxWritesAtOnce();
	TestBulkChainDefaultsToOneQueuedPacket();
	TestStagedPacketDroppedWhenDownUnderFail();
	TestDownWaitIsBusy();
	TestDownFailIsNoConnection();
	TestSizeWithoutParts();
	TestReceiveBufferIsMaxPayload();
	TestReceiveLargerThanMaxPayload();
	TestConcurrentWithoutStagingRing();
	TestStagedSendWaitsForProcess();
	TestWaitRefusesAfterReportedFall();
	TestFailRefusesAfterReportedFall();
	TestPacketReadyRefusesAfterReportedFall();
	return SimFinish("TestVubStackMinimal");
}
