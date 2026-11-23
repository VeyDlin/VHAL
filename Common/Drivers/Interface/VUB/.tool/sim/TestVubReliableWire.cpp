#include "SimCheck.h"
#include "ReliableChains.h"
#include <Drivers/Interface/VUB/TestKit/Loopback.h>
#include <Utilities/DataTypes.h>
#include <array>
#include <cstddef>
#include <span>

// The bytes of the delivery header, checked against the layout of the wiki page (Layer/VUB::Reliable): flags first, then for a sequenced
// packet its tag, its number and the window base delta, for a sync the boot value and the session counter, and for an acknowledgement its
// tag and number. The tests give the boot value 0x1234 with SetSession, so the first session has counter 0 and tag 0x34. The
// expected bytes below are written by hand from that layout.


// One round of a packet line: the near stack sends, the far stack gets the packets and answers, and the answers come back.
template<typename Stack>
void WireRound(PacketLine& line, Stack& near, Stack& far) {
	near.Process();
	line.DeliverToB(far);
	far.Process();
	line.DeliverToA(near);
	near.Process();
}


// Starts the session with a Send, which is busy until the sync is acknowledged, and runs one round: the sync goes out and its
// acknowledgement comes back, so the next Send is accepted.
template<typename Stack>
void EstablishSession(PacketLine& line, Stack& near, Stack& far) {
	std::array<uint8, 2> trigger = {0x00, 0x00};
	SIM_EXPECT(near.Send(std::span<const uint8>(trigger.data(), trigger.size())) == ResultStatus::busy);
	WireRound(line, near, far);
}


// The sync: flags 0x05 (sequenced, sync), the tag 0x34 (counter 0 plus the boot value's low byte), the number 0x0000 (the first number
// of a session), the base delta 0x00, the boot value 0x1234 little-endian, and the counter 0x00. It carries no payload.
void TestSyncPacketBytes() {
	ResetReliableState();
	testNowUs = 0x1234;
	PacketLine line(VUB::Faults{});
	ReliableWire near(line.a);
	GiveSession(near);
	near.Process();
	// The first Send starts the session. It is busy: the data waits for the sync's acknowledgement.
	std::array<uint8, 3> payload = {0x11, 0x00, 0x22};
	SIM_EXPECT(near.Send(std::span<const uint8>(payload.data(), payload.size())) == ResultStatus::busy);
	near.Process();
	ReliableCapture capture;
	line.DeliverToB(capture);
	SIM_EXPECT(capture.count == 1);
	std::array<uint8, 8> sync = {0x05, 0x34, 0x00, 0x00, 0x00, 0x34, 0x12, 0x00};
	SIM_EXPECT(CaptureIs(capture, 0, sync));
}


// After the sync is acknowledged the window base is the first number of the session, so the first data packet has delta 0. The second
// one, sent while the first is unacknowledged, has delta 1: its base is the first packet's number.
void TestSequencedPacketBytes() {
	ResetReliableState();
	testNowUs = 0x1234;
	PacketLine line(VUB::Faults{});
	ReliableWire near(line.a);
	GiveSession(near);
	ReliableWire far(line.b);
	GiveSession(far);
	near.Process();
	far.Process();
	ReliableCapture discard;
	line.DeliverToB(discard);
	EstablishSession(line, near, far);
	std::array<uint8, 3> first = {0x11, 0x00, 0x22};
	std::array<uint8, 2> second = {0x33, 0x00};
	SIM_EXPECT(near.Send(std::span<const uint8>(first.data(), first.size())) == ResultStatus::ok);
	SIM_EXPECT(near.Send(std::span<const uint8>(second.data(), second.size())) == ResultStatus::ok);
	near.Process();
	ReliableCapture capture;
	line.DeliverToB(capture);
	SIM_EXPECT(capture.count == 2);
	// Flags 0x01, tag 0x34, number 0x0001, delta 0x00, then the payload.
	std::array<uint8, 8> firstExpected = {0x01, 0x34, 0x01, 0x00, 0x00, 0x11, 0x00, 0x22};
	SIM_EXPECT(CaptureIs(capture, 0, firstExpected));
	// Flags 0x01, tag 0x34, number 0x0002, delta 0x01 (the first packet's number is the base), then the payload.
	std::array<uint8, 7> secondExpected = {0x01, 0x34, 0x02, 0x00, 0x01, 0x33, 0x00};
	SIM_EXPECT(CaptureIs(capture, 1, secondExpected));
}


// The receiver acknowledges the sync with a bare acknowledgement: flags 0x12 (control, acknowledgement), the tag of the session, then the
// number acknowledged, 0x0000. The sender then accepts Send.
void TestBareAckBytes() {
	ResetReliableState();
	testNowUs = 0x1234;
	PacketLine line(VUB::Faults{});
	ReliableWire near(line.a);
	GiveSession(near);
	ReliableWire far(line.b);
	GiveSession(far);
	near.Process();
	far.Process();
	std::array<uint8, 3> payload = {0x11, 0x00, 0x22};
	SIM_EXPECT(near.Send(std::span<const uint8>(payload.data(), payload.size())) == ResultStatus::busy);
	near.Process();
	ReliableCapture toFar;
	line.DeliverToB(toFar);
	SIM_EXPECT(toFar.count == 1);
	far.FeedPacket(std::span<const uint8>(toFar.packets[0].data(), toFar.lengths[0]));
	far.Process();

	ReliableCapture toNear;
	line.DeliverToA(toNear);
	std::array<uint8, 4> ack = {0x12, 0x34, 0x00, 0x00};
	SIM_EXPECT(toNear.count == 1);
	SIM_EXPECT(CaptureIs(toNear, 0, ack));

	near.FeedPacket(std::span<const uint8>(toNear.packets[0].data(), toNear.lengths[0]));
	near.Process();
	SIM_EXPECT(near.Send(std::span<const uint8>(payload.data(), payload.size())) == ResultStatus::ok);
}


// A receiver that has no state for the sender, and gets a sequenced packet of a session it does not hold, asks for a session with a
// resync request: flags 0x08, then the tag the request refers to. The packet is not delivered and not acknowledged.
void TestResyncRequestBytes() {
	ResetReliableState();
	testNowUs = 0x1234;
	PacketLine line(VUB::Faults{});
	ReliableWire far(line.b);
	GiveSession(far);
	far.Process();
	std::array<uint8, 8> data = {0x01, 0x34, 0x01, 0x00, 0x00, 0x11, 0x00, 0x22};
	far.FeedPacket(std::span<const uint8>(data.data(), data.size()));
	far.Process();
	SIM_EXPECT(testDelivered[0x11] == 0);
	ReliableCapture toNear;
	line.DeliverToA(toNear);
	std::array<uint8, 2> resync = {0x08, 0x34};
	SIM_EXPECT(toNear.count == 1);
	SIM_EXPECT(CaptureIs(toNear, 0, resync));
}


// A packet with the no-acknowledgement flag has no sequence number, no tag and no delta: flags 0x00, then the payload.
void TestUnsequencedPacketBytes() {
	ResetReliableState();
	PacketLine line(VUB::Faults{});
	ReliableWire near(line.a);
	GiveSession(near);
	near.Process();
	ReliableCapture discard;
	line.DeliverToB(discard);
	std::array<uint8, 2> payload = {0xAB, 0x00};
	VUB::Meta meta{};
	meta.flags = VUB::Meta::NoAcknowledgeFlag;
	SIM_EXPECT(near.Send(std::span<const uint8>(payload.data(), payload.size()), meta) == ResultStatus::ok);
	near.Process();
	ReliableCapture capture;
	line.DeliverToB(capture);
	SIM_EXPECT(capture.count == 1);
	std::array<uint8, 3> expected = {0x00, 0xAB, 0x00};
	SIM_EXPECT(CaptureIs(capture, 0, expected));
}


// The reply hook: a packet sent from the receive handler carries the acknowledgement it owes, so no bare acknowledgement follows.
ReliableWire* testEchoStack = nullptr;


void EchoUnsequencedOnDeliver(uint16 id) {
	(void)id;
	std::array<uint8, 2> reply = {0xCD, 0x00};
	VUB::Meta meta{};
	meta.flags = VUB::Meta::NoAcknowledgeFlag;
	testEchoStack->Send(std::span<const uint8>(reply.data(), reply.size()), meta);
}


// The unsequenced reply carries the owed acknowledgement: flags 0x02, the tag and the acknowledged number 0x0001, then the payload. Only
// one packet goes out.
void TestAcknowledgementRidesOnReply() {
	ResetReliableState();
	testNowUs = 0x1234;
	PacketLine line(VUB::Faults{});
	ReliableWire near(line.a);
	GiveSession(near);
	ReliableWire far(line.b);
	GiveSession(far);
	near.Process();
	far.Process();
	EstablishSession(line, near, far);
	ReliableCapture discard;
	line.DeliverToA(discard);
	std::array<uint8, 3> payload = {0x11, 0x00, 0x22};
	SIM_EXPECT(near.Send(std::span<const uint8>(payload.data(), payload.size())) == ResultStatus::ok);
	near.Process();
	ReliableCapture toFar;
	line.DeliverToB(toFar);
	SIM_EXPECT(toFar.count == 1);
	far.FeedPacket(std::span<const uint8>(toFar.packets[0].data(), toFar.lengths[0]));
	testEchoStack = &far;
	testDeliverHook = &EchoUnsequencedOnDeliver;
	far.Process();
	testDeliverHook = nullptr;
	testEchoStack = nullptr;
	ReliableCapture toNear;
	line.DeliverToA(toNear);
	SIM_EXPECT(toNear.count == 1);
	std::array<uint8, 6> reply = {0x02, 0x34, 0x01, 0x00, 0xCD, 0x00};
	SIM_EXPECT(CaptureIs(toNear, 0, reply));
}


// The packets that carry an acknowledgement with a sync or a sequenced packet, written by hand. A sync from the far side, with a boot value
// of 0x5656 and counter 0, acknowledging the near side's first packet (flags 0x07), and then a sequenced packet of that session with the
// acknowledgement of the near side's sync (flags 0x03). The near side answers each with the bare acknowledgement of the number it received.
void TestSyncAndSequencedPacketsWithAcknowledgementBytes() {
	ResetReliableState();
	testNowUs = 0x1234;
	PacketLine line(VUB::Faults{});
	ReliableWire near(line.a);
	GiveSession(near);
	near.Process();
	std::array<uint8, 3> payload = {0x09, 0x00, 0x00};
	SIM_EXPECT(near.Send(std::span<const uint8>(payload.data(), payload.size())) == ResultStatus::busy);
	near.Process();
	ReliableCapture discard;
	line.DeliverToB(discard);
	// The near side's session is acknowledged by hand: a bare acknowledgement of its sync.
	std::array<uint8, 4> syncAck = {0x12, 0x34, 0x00, 0x00};
	near.FeedPacket(std::span<const uint8>(syncAck.data(), syncAck.size()));
	near.Process();
	SIM_EXPECT(near.Send(std::span<const uint8>(payload.data(), payload.size())) == ResultStatus::ok);
	near.Process();
	line.DeliverToB(discard);

	// Flags 0x07: the sync (tag 0x56, boot 0x5656, counter 0x00), acknowledging the near side's first packet (tag 0x34, number 0x0001).
	std::array<uint8, 11> farSync = {0x07, 0x56, 0x00, 0x00, 0x00, 0x56, 0x56, 0x00, 0x34, 0x01, 0x00};
	near.FeedPacket(std::span<const uint8>(farSync.data(), farSync.size()));
	near.Process();
	SIM_EXPECT(testStatus[9] == ResultStatus::ok);
	SIM_EXPECT(testOutcomes[9] == 1);
	ReliableCapture owed;
	line.DeliverToB(owed);
	// The sync is not passed up. The bare acknowledgement of the sync is owed, with the far side's tag.
	SIM_EXPECT(testDelivered[0x0000] == 0);
	std::array<uint8, 4> syncOwed = {0x12, 0x56, 0x00, 0x00};
	SIM_EXPECT(owed.count == 1);
	SIM_EXPECT(CaptureIs(owed, 0, syncOwed));

	// Flags 0x03: a sequenced packet (tag 0x56, number 0x0001, delta 0x00) with an acknowledgement (tag 0x34, number 0x0000). The payload is
	// id 7, and it is passed up.
	std::array<uint8, 10> farData = {0x03, 0x56, 0x01, 0x00, 0x00, 0x34, 0x00, 0x00, 0x07, 0x00};
	near.FeedPacket(std::span<const uint8>(farData.data(), farData.size()));
	near.Process();
	SIM_EXPECT(testDelivered[7] == 1);
	ReliableCapture answer;
	line.DeliverToB(answer);
	std::array<uint8, 4> dataOwed = {0x12, 0x56, 0x01, 0x00};
	SIM_EXPECT(answer.count == 1);
	SIM_EXPECT(CaptureIs(answer, 0, dataOwed));
}


// 8-bit sequence numbers: the sync has one byte of number, and the bare acknowledgement the same. The delta is one byte in both widths.
void TestEightBitBytes() {
	ResetReliableState();
	testNowUs = 0x1234;
	PacketLine line(VUB::Faults{});
	ReliableWire8 near(line.a);
	GiveSession(near);
	ReliableWire8 far(line.b);
	GiveSession(far);
	near.Process();
	std::array<uint8, 3> payload = {0x11, 0x00, 0x22};
	SIM_EXPECT(near.Send(std::span<const uint8>(payload.data(), payload.size())) == ResultStatus::busy);
	near.Process();
	ReliableCapture toFar;
	line.DeliverToB(toFar);
	std::array<uint8, 7> sync = {0x05, 0x34, 0x00, 0x00, 0x34, 0x12, 0x00};
	SIM_EXPECT(CaptureIs(toFar, 0, sync));
	far.FeedPacket(std::span<const uint8>(toFar.packets[0].data(), toFar.lengths[0]));
	far.Process();
	ReliableCapture toNear;
	line.DeliverToA(toNear);
	std::array<uint8, 3> ack = {0x12, 0x34, 0x00};
	SIM_EXPECT(CaptureIs(toNear, 0, ack));
}


// Headers that do not follow the layout are refused and counted as layer errors: an unknown flag, a sync with a payload, a resync with a
// byte more, a control without acknowledgement, a sequenced packet cut short, a bare acknowledgement with a byte more, a sync cut short,
// and a sync whose tag is not the tag of its boot value and counter.
void TestMalformedHeadersAreRefused() {
	ResetReliableState();
	PacketLine line(VUB::Faults{});
	ReliableWire near(line.a);
	GiveSession(near);
	near.Process();
	std::array<std::array<uint8, 9>, 8> bad = {{
		{0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
		{0x05, 0x34, 0x00, 0x00, 0x00, 0x34, 0x12, 0x00, 0xAA},
		{0x08, 0x34, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
		{0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
		{0x01, 0x34, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
		{0x12, 0x34, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
		{0x05, 0x34, 0x00, 0x00, 0x00, 0x34, 0x12, 0x00, 0x00},
		{0x05, 0x35, 0x00, 0x00, 0x00, 0x34, 0x12, 0x00, 0x00},
	}};
	std::array<std::size_t, 8> lengths = {1, 9, 3, 2, 4, 5, 7, 8};
	for (std::size_t index = 0; index < bad.size(); index++) {
		near.FeedPacket(std::span<const uint8>(bad[index].data(), lengths[index]));
		near.Process();
	}

	SIM_EXPECT(near.Drops().layerError == 8);
}


int main() {
	TestSyncPacketBytes();
	TestSequencedPacketBytes();
	TestBareAckBytes();
	TestResyncRequestBytes();
	TestUnsequencedPacketBytes();
	TestAcknowledgementRidesOnReply();
	TestSyncAndSequencedPacketsWithAcknowledgementBytes();
	TestEightBitBytes();
	TestMalformedHeadersAreRefused();
	return SimFinish("TestVubReliableWire");
}
