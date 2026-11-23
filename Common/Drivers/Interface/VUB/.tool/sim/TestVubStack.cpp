#include "SimCheck.h"
#include "Chains.h"
#include <Drivers/Interface/VUB/TestKit/Random.h>
#include <Drivers/Interface/VUB/Meta.h>
#include <Utilities/DataTypes.h>
#include <array>
#include <cstddef>
#include <span>


// The first packet of the stream chain, by hand. Layer header 00, sequence 00 00, payload 01: plain 00 00 00 01. Encrypted with
// A5, 80, EF and CB (the last is 01 XOR CA). The sum is DF and the XOR is 01. The flag framing sees the six bytes A5 80 EF CB DF 01,
// adds its checks BF (sum) and DF (XOR), and frames them between flags 7E. The wire carries exactly ten bytes.
void TestWireBytesByHand() {
	ResetTestState();
	StreamLine line(VUB::Faults{});
	StreamStack sender(line.a);
	std::array<uint8, 1> payload = {0x01};
	SIM_EXPECT(sender.Send(std::span<const uint8>(payload.data(), payload.size())) == ResultStatus::ok);
	sender.Process();

	RecordSink sink;
	line.DeliverToB(sink);
	const std::array<uint8, 10> expected = {0x7E, 0xA5, 0x80, 0xEF, 0xCB, 0xDF, 0x01, 0xBF, 0xDF, 0x7E};
	SIM_EXPECT(sink.byteCount == 10);
	for (std::size_t index = 0; index < expected.size() && index < sink.byteCount; index++) {
		SIM_EXPECT(sink.bytes[index] == expected[index]);
	}
}


// Twenty random packets through the stream chain, sent one at a time, come out whole and in order.
void TestRoundTripStream() {
	ResetTestState();
	StreamLine line(VUB::Faults{});
	StreamStack sender(line.a);
	StreamStack receiver(line.b);
	VUB::Random random(11);
	std::array<std::array<uint8, 40>, 20> sent{};
	std::array<std::size_t, 20> sentLength{};
	for (std::size_t index = 0; index < 20; index++) {
		std::size_t length = 1 + random.Below(40);
		for (std::size_t byteIndex = 0; byteIndex < length; byteIndex++) {
			sent[index][byteIndex] = static_cast<uint8>(random.Next());
		}

		sentLength[index] = length;
		SIM_EXPECT(sender.Send(std::span<const uint8>(sent[index].data(), length)) == ResultStatus::ok);
		sender.Process();
		line.DeliverToB(receiver);
		receiver.Process();
	}

	SIM_EXPECT(testReceivedCount == 20);
	for (std::size_t index = 0; index < 20 && index < testReceivedCount; index++) {
		SIM_EXPECT(testReceivedLength[index] == sentLength[index]);
		for (std::size_t byteIndex = 0; byteIndex < sentLength[index]; byteIndex++) {
			SIM_EXPECT(testReceived[index][byteIndex] == sent[index][byteIndex]);
		}
	}
}


// A stream link that takes 12 bytes a round. A frame of the 10-byte payload is 19 bytes on the wire: 10 payload, 3 layer header, 2
// XOR check bytes, then the flag framing's 2 check bytes and 2 flags. The 6 frames are 114 bytes, so 12 bytes a round leave the
// stack as 12 * round bytes, and a frame has arrived once its closing flag does: after each round the receiver has (12 * round) / 19
// frames, and all 6 arrive in 10 rounds.
void TestStreamBackpressure() {
	ResetTestState();
	StreamLine line(VUB::Faults{});
	StreamStack sender(line.a);
	StreamStack receiver(line.b);
	const std::size_t frameBytes = 19;
	const std::size_t frames = 6;
	std::size_t sentCount = 0;
	std::size_t rounds = 0;
	bool perRoundOk = true;
	while (testReceivedCount < frames && rounds < 100) {
		if (sentCount < frames) {
			std::array<uint8, 10> payload{};
			payload[0] = static_cast<uint8>(sentCount);
			if (sender.Send(std::span<const uint8>(payload.data(), payload.size())) == ResultStatus::ok) {
				sentCount++;
			}
		}

		line.a.StartRound(12);
		sender.Process();
		line.DeliverToB(receiver);
		receiver.Process();
		rounds++;
		std::size_t written = 12 * rounds < frames * frameBytes ? 12 * rounds : frames * frameBytes;
		perRoundOk = perRoundOk && testReceivedCount == written / frameBytes;
	}

	SIM_EXPECT(perRoundOk);
	SIM_EXPECT(testReceivedCount == frames);
	SIM_EXPECT(rounds == 10);
	for (std::size_t index = 0; index < frames && index < testReceivedCount; index++) {
		SIM_EXPECT(testReceived[index][0] == index);
		SIM_EXPECT(testReceivedLength[index] == 10);
	}
}


// A packet link that takes one packet a round: after each round the receiver has exactly one more, and all four come out in order.
void TestPacketBackpressure() {
	ResetTestState();
	PacketLine line(VUB::Faults{});
	PacketStack sender(line.a);
	PacketStack receiver(line.b);
	for (std::size_t index = 0; index < 4; index++) {
		std::array<uint8, 2> payload = {static_cast<uint8>(index), 0x33};
		SIM_EXPECT(sender.Send(std::span<const uint8>(payload.data(), payload.size())) == ResultStatus::ok);
	}

	for (std::size_t round = 1; round <= 4; round++) {
		line.a.StartRound(1);
		sender.Process();
		line.DeliverToB(receiver);
		receiver.Process();
		SIM_EXPECT(testReceivedCount == round);
	}

	for (std::size_t index = 0; index < 4 && index < testReceivedCount; index++) {
		SIM_EXPECT(testReceived[index][0] == index);
	}
}


// The control queue holds four packets. A fifth Send while it is full returns busy, and nothing is dropped: the four still go out.
void TestSendBusyWhenQueueFull() {
	ResetTestState();
	PacketLine line(VUB::Faults{});
	PacketStack sender(line.a);
	PacketStack receiver(line.b);
	line.a.StartRound(0);
	std::array<uint8, 2> payload = {0x01, 0x02};
	for (std::size_t index = 0; index < 4; index++) {
		SIM_EXPECT(sender.Send(std::span<const uint8>(payload.data(), payload.size())) == ResultStatus::ok);
	}

	SIM_EXPECT(sender.Send(std::span<const uint8>(payload.data(), payload.size())) == ResultStatus::busy);
	line.a.StartRound(10);
	sender.Process();
	line.DeliverToB(receiver);
	receiver.Process();
	SIM_EXPECT(testReceivedCount == 4);
}


// An application packet sent while the bulk layer's packet waits for the link goes out first. The bulk packet follows.
void TestApplicationBeforeBulk() {
	ResetTestState();
	PacketLine line(VUB::Faults{});
	BulkStack sender(line.a);
	BulkStack receiver(line.b);

	line.a.StartRound(0);
	sender.Process();
	line.DeliverToB(receiver);
	receiver.Process();
	SIM_EXPECT(testReceivedCount == 0);

	std::array<uint8, 1> application = {0xAA};
	SIM_EXPECT(sender.Send(std::span<const uint8>(application.data(), application.size())) == ResultStatus::ok);

	line.a.StartRound(1);
	sender.Process();
	line.DeliverToB(receiver);
	receiver.Process();
	SIM_EXPECT(testReceivedCount == 1);
	SIM_EXPECT(testReceivedLength[0] == 1);
	SIM_EXPECT(testReceived[0][0] == 0xAA);

	line.a.StartRound(1);
	sender.Process();
	line.DeliverToB(receiver);
	receiver.Process();
	SIM_EXPECT(testReceivedCount == 2);
	SIM_EXPECT(testReceived[1][0] == 0xBB);
}


// Process sends at most PerProcess packets a call, so a bulk layer that always has data cannot hold the call.
void TestProcessDoesNotHoldOnBulk() {
	ResetTestState();
	PacketLine line(VUB::Faults{});
	BulkStack sender(line.a);
	sender.Process();
	SIM_EXPECT(line.toB.Counts().pushed == 8);
}


// The flags of a packet are set by the block that makes it, and read by every block below it: the application's flags reach the
// probe at Send time, and the bulk layer's flag reaches it when the bulk packet is pulled.
void TestFlagsReachBottom() {
	ResetTestState();
	testProbeFlags = 0;
	PacketLine line(VUB::Faults{});
	ProbeStack sender(line.a);

	VUB::Meta meta{};
	meta.flags = 0x02;
	std::array<uint8, 1> application = {0x11};
	SIM_EXPECT(sender.Send(std::span<const uint8>(application.data(), application.size()), meta) == ResultStatus::ok);
	SIM_EXPECT(testProbeFlags == 0x02);

	line.a.StartRound(1);
	sender.Process();
	SIM_EXPECT(testProbeFlags == 0x01);
}


// Counters: the ring keeps 256 bytes, so 300 bytes fed without a Process lose 44, counted as ring full.
void TestRingFullCounted() {
	ResetTestState();
	StreamLine line(VUB::Faults{});
	StreamStack receiver(line.b);
	for (std::size_t index = 0; index < 300; index++) {
		receiver.Feed(0x00);
	}

	SIM_EXPECT(receiver.Drops().ringFull == 44);
}


// A packet longer than the receive buffer (64) is counted as too long; a fifth packet into a queue of four is counted as queue full.
void TestPacketDropsCounted() {
	ResetTestState();
	PacketLine line(VUB::Faults{});
	PacketStack receiver(line.b);
	std::array<uint8, 65> big{};
	receiver.FeedPacket(std::span<const uint8>(big.data(), big.size()));
	SIM_EXPECT(receiver.Drops().tooLong == 1);

	std::array<uint8, 3> small = {1, 2, 3};
	for (std::size_t index = 0; index < 5; index++) {
		receiver.FeedPacket(std::span<const uint8>(small.data(), small.size()));
	}

	SIM_EXPECT(receiver.Drops().queueFull == 1);
}


// Two bad packets, each counted by its own reason. {1,2,3,4,5}: the XOR check of the first three bytes is 06 and 00, and the packet
// says 04 and 05, so the transform refuses it. {A5,80,25,25} decodes to the two bytes 00 00, shorter than the 3-byte header, so the
// layer refuses it.
void TestTransformAndLayerErrorsCounted() {
	ResetTestState();
	PacketLine line(VUB::Faults{});
	PacketStack receiver(line.b);
	std::array<uint8, 5> bad = {1, 2, 3, 4, 5};
	receiver.FeedPacket(std::span<const uint8>(bad.data(), bad.size()));
	receiver.Process();
	SIM_EXPECT(receiver.Drops().transformError == 1);

	std::array<uint8, 4> shortPacket = {0xA5, 0x80, 0x25, 0x25};
	receiver.FeedPacket(std::span<const uint8>(shortPacket.data(), shortPacket.size()));
	receiver.Process();
	SIM_EXPECT(receiver.Drops().layerError == 1);
	SIM_EXPECT(testReceivedCount == 0);
}


// A beacon is sent from Process at each interval, goes through the transform and the framing, and is taken by the layer on the far
// side without reaching the handler.
void TestBeaconSentAndTaken() {
	ResetTestState();
	StreamLine line(VUB::Faults{});
	BeaconStack sender(line.a);
	StreamStack receiver(line.b);

	testNowUs = 0;
	sender.Process();
	testNowUs = 500;
	sender.Process();
	testNowUs = 1000;
	sender.Process();
	SIM_EXPECT(testBeaconsSent == 2);

	line.DeliverToB(receiver);
	receiver.Process();
	SIM_EXPECT(testBeaconsTaken == 2);
	SIM_EXPECT(testReceivedCount == 0);
}


// Packets over a faulty packet line: every packet the wire delivers ends in the handler or in one counted reason. The corrupted ones
// fail the XOR check, the duplicates arrive twice, and none is lost without a count.
void TestPacketFaultsAccounted() {
	ResetTestState();
	VUB::Faults faults;
	faults.seed = 21;
	faults.dropPercent = 10;
	faults.corruptPercent = 10;
	faults.duplicatePercent = 10;
	faults.maxDelayUs = 30;
	PacketLine line(faults);
	PacketStack sender(line.a);
	PacketStack receiver(line.b);

	std::size_t sentCount = 0;
	for (uint32 step = 0; step < 400; step++) {
		testNowUs = step * 10;
		line.SetNow(step * 10);
		if (sentCount < 100) {
			std::array<uint8, 4> payload = {static_cast<uint8>(sentCount), 0x10, 0x20, 0x30};
			if (sender.Send(std::span<const uint8>(payload.data(), payload.size())) == ResultStatus::ok) {
				sentCount++;
			}
		}

		sender.Process();
		line.DeliverToB(receiver);
		receiver.Process();
	}

	SIM_EXPECT(sentCount == 100);
	VUB::WireCounts counts = line.toB.Counts();
	SIM_EXPECT(counts.dropped > 0);
	SIM_EXPECT(counts.corrupted > 0);
	SIM_EXPECT(counts.duplicated > 0);

	VUB::DropCounters drops = receiver.Drops();
	SIM_EXPECT(drops.transformError > 0);
	std::size_t stackDrops = drops.transformError + drops.layerError + drops.tooLong + drops.queueFull;
	SIM_EXPECT(testReceivedCount + stackDrops == counts.delivered);
}


// Packets over a faulty stream: every payload the handler gets is one of the payloads sent, in the order sent, and the lost or changed
// frames are counted as framing errors.
void TestStreamFaultsOnlyWholePackets() {
	ResetTestState();
	VUB::Faults faults;
	faults.seed = 31;
	faults.dropPercent = 3;
	faults.corruptPercent = 3;
	StreamLine line(faults);
	StreamStack sender(line.a);
	StreamStack receiver(line.b);

	VUB::Random random(31);
	std::array<std::array<uint8, 40>, 60> sent{};
	std::array<std::size_t, 60> sentLength{};
	std::size_t sentCount = 0;
	for (uint32 step = 0; step < 200; step++) {
		testNowUs = step;
		line.SetNow(step);
		if (sentCount < 60) {
			std::size_t length = 1 + random.Below(40);
			sent[sentCount][0] = static_cast<uint8>(sentCount);
			for (std::size_t byteIndex = 1; byteIndex < length; byteIndex++) {
				sent[sentCount][byteIndex] = static_cast<uint8>(random.Next());
			}

			sentLength[sentCount] = length;
			if (sender.Send(std::span<const uint8>(sent[sentCount].data(), length)) == ResultStatus::ok) {
				sentCount++;
			}
		}

		sender.Process();
		line.DeliverToB(receiver);
		receiver.Process();
	}

	SIM_EXPECT(testReceivedCount > 0);
	SIM_EXPECT(receiver.Drops().framingError > 0);

	std::size_t candidate = 0;
	bool inOrder = true;
	for (std::size_t index = 0; index < testReceivedCount && index < 256; index++) {
		bool found = false;
		while (candidate < sentCount && !found) {
			bool same = testReceivedLength[index] == sentLength[candidate];
			for (std::size_t byteIndex = 0; byteIndex < testReceivedLength[index] && same; byteIndex++) {
				same = testReceived[index][byteIndex] == sent[candidate][byteIndex];
			}

			if (same) {
				found = true;
			}

			candidate++;
		}

		inOrder = inOrder && found;
	}

	SIM_EXPECT(inOrder);
}


// The bulk layer always has data (its limit is 1000 packets, so a loop that never ends stops with a wrong count), and the framing refuses
// every bulk packet. Each Process call takes exactly PerProcess (8) bulk packets: a refused packet counts toward PerProcess like a sent
// one, and it is counted as a send error.
using RefusingStack = VUB::Stack<+[] {
	return VUB::Builder()
		.Layer<TestLayer>(TestLayerOptions{0, true, 1000})
		.Transform<XorTransform>()
		.Framing<BulkRefusingFraming>()
		.Link<StreamEnd>()
		.MaxPayload(40)
		.ReceiveRing(256)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


void TestRefusedBulkCountsTowardPerProcess() {
	ResetTestState();
	StreamLine line(VUB::Faults{});
	RefusingStack stack(line.a);
	stack.Process();
	SIM_EXPECT(stack.Drops().sendError == 8);
	stack.Process();
	SIM_EXPECT(stack.Drops().sendError == 16);
	SIM_EXPECT(line.toB.Counts().pushed == 0);
}


// A layer whose Process sends one packet of 100 bytes, more than the 40-byte packet size of its chain. The packet has no caller, so the
// stack counts it. Send refuses a too long payload to its caller and counts nothing.
struct OversizeLayer {
	static constexpr std::size_t header = 0;
	static constexpr uint32 fingerprint = 0x4F565A31;
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
		if (sent) {
			return;
		}

		sent = true;
		std::array<uint8, 100> big{};
		(void)outbox.Send(std::span<const uint8>(big.data(), big.size()));
	}


	bool sent = false;
};


using OversizeStack = VUB::Stack<+[] {
	return VUB::Builder()
		.Layer<OversizeLayer>()
		.Link<PacketEnd>()
		.MaxPayload(40)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


void TestOutboxPacketTooLongCounted() {
	ResetTestState();
	PacketLine line(VUB::Faults{});
	OversizeStack stack(line.a);
	std::array<uint8, 41> tooLong{};
	SIM_EXPECT(stack.Send(std::span<const uint8>(tooLong.data(), tooLong.size())) == ResultStatus::messageTooLong);
	SIM_EXPECT(stack.Drops().layerError == 0);
	stack.Process();
	SIM_EXPECT(stack.Drops().layerError == 1);
	SIM_EXPECT(line.toB.Counts().pushed == 0);
}


// The status the layer below the sending layer answers for the packet. ok lets the packet through.
inline ResultStatus testOwnAddStatus = ResultStatus::ok;


// A layer that sends one packet of its own from each Process. The packet goes through the layers below it, so the layer below refuses it.
struct OwnSendLayer {
	static constexpr std::size_t header = 1;
	static constexpr uint32 fingerprint = 0x4F574E31;
	static constexpr bool usesTime = false;
	static constexpr bool makesBulk = false;


	Result<std::size_t> Add(const VUB::Meta&, std::span<const uint8> in, std::span<uint8> out) {
		if (out.size() < in.size() + header) {
			return ResultStatus::invalidParameter;
		}

		out[0] = 0;
		for (std::size_t index = 0; index < in.size(); index++) {
			out[header + index] = in[index];
		}

		return in.size() + header;
	}


	Result<VUB::Taken> Take(const VUB::Meta&, std::span<const uint8> packet) {
		if (packet.size() < header) {
			return ResultStatus::invalidData;
		}

		return VUB::Taken{header, true};
	}


	void Process(uint32, const VUB::Outbox& outbox) {
		std::array<uint8, 1> packet{};
		(void)outbox.Send(std::span<const uint8>(packet.data(), packet.size()));
	}
};


// A layer below the sending layer. Its Add answers testOwnAddStatus: busy keeps the packet with the sender, and an error is a real one.
struct OwnRefuseLayer {
	static constexpr std::size_t header = 1;
	static constexpr uint32 fingerprint = 0x4F574E32;
	static constexpr bool usesTime = false;
	static constexpr bool makesBulk = false;


	Result<std::size_t> Add(const VUB::Meta&, std::span<const uint8> in, std::span<uint8> out) {
		if (testOwnAddStatus != ResultStatus::ok) {
			return testOwnAddStatus;
		}

		if (out.size() < in.size() + header) {
			return ResultStatus::invalidParameter;
		}

		out[0] = 0;
		for (std::size_t index = 0; index < in.size(); index++) {
			out[header + index] = in[index];
		}

		return in.size() + header;
	}


	Result<VUB::Taken> Take(const VUB::Meta&, std::span<const uint8> packet) {
		if (packet.size() < header) {
			return ResultStatus::invalidData;
		}

		return VUB::Taken{header, true};
	}


	void Process(uint32, const VUB::Outbox&) {
	}
};


using OwnSendStack = VUB::Stack<+[] {
	return VUB::Builder()
		.Layer<OwnSendLayer>()
		.Layer<OwnRefuseLayer>()
		.Link<PacketEnd>()
		.MaxPayload(40)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


// A layer's own packet that its Add refuses busy is not a drop: the layer keeps it and sends it again, so layerError stays at 0 however
// many times it is refused. A real Add error on the same path still counts.
void TestOwnBusyNotCounted() {
	ResetTestState();
	PacketLine line(VUB::Faults{});
	OwnSendStack stack(line.a);
	testOwnAddStatus = ResultStatus::busy;
	for (int round = 0; round < 3; round++) {
		stack.Process();
	}

	SIM_EXPECT(stack.Drops().layerError == 0);
	testOwnAddStatus = ResultStatus::invalidParameter;
	stack.Process();
	SIM_EXPECT(stack.Drops().layerError == 1);
	testOwnAddStatus = ResultStatus::ok;
}


int main() {
	TestWireBytesByHand();
	TestRoundTripStream();
	TestStreamBackpressure();
	TestPacketBackpressure();
	TestSendBusyWhenQueueFull();
	TestApplicationBeforeBulk();
	TestProcessDoesNotHoldOnBulk();
	TestFlagsReachBottom();
	TestRingFullCounted();
	TestPacketDropsCounted();
	TestTransformAndLayerErrorsCounted();
	TestBeaconSentAndTaken();
	TestPacketFaultsAccounted();
	TestStreamFaultsOnlyWholePackets();
	TestRefusedBulkCountsTowardPerProcess();
	TestOutboxPacketTooLongCounted();
	TestOwnBusyNotCounted();
	return SimFinish("TestVubStack");
}
