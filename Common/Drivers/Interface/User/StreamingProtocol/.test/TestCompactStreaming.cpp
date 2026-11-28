#include <TestHelpers.h>
#include <TestRunner.h>

#include <VHAL.h>
#include <Drivers/Interface/User/StreamingProtocol/CompactStreamingProtocol.h>

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using Protocol = CompactStreamingProtocol<>;
using TestHelpers::AsBytes;
using TestHelpers::ResetSystem;


namespace {


// Wire format v2, typeAndFlags: bit 0 packet type (1 = ACK), bit 1 last fragment of the message, bits 2..7 message id (0..63).
constexpr uint8 kCspAckBit = 0x01;
constexpr uint8 kCspLastBit = 0x02;


// CRC-8/CCITT (poly 0x07, init 0xFF) over the fragment header and payload.
uint8 Crc8Ccitt(std::span<const uint8> data) {
	uint8 crc = 0xFF;

	for (const uint8 byte : data) {
		crc = static_cast<uint8>(crc ^ byte);

		for (int bit = 0; bit < 8; bit++) {
			if ((crc & 0x80) != 0) {
				crc = static_cast<uint8>((crc << 1) ^ 0x07);
			} else {
				crc = static_cast<uint8>(crc << 1);
			}
		}
	}

	return crc;
}


// typeAndFlags of a data fragment of message messageId; last is set on the final fragment of the message.
uint8 CspDataFlags(uint8 messageId, bool last) {
	uint8 flags = static_cast<uint8>(messageId << 2);

	if (last) {
		flags = static_cast<uint8>(flags | kCspLastBit);
	}

	return flags;
}


// typeAndFlags of an ACK for a fragment of message messageId (the last bit is always clear in an ACK).
uint8 CspAckFlags(uint8 messageId) {
	return static_cast<uint8>((messageId << 2) | kCspAckBit);
}


// CompactStreamingProtocol fragment: typeAndFlags, payloadSize, fragmentIndex, payload, CRC-8.
std::vector<uint8> BuildFragment(uint8 typeAndFlags, uint8 fragmentIndex, std::span<const uint8> payload) {
	std::vector<uint8> frame = { typeAndFlags, static_cast<uint8>(payload.size()), fragmentIndex };
	frame.insert(frame.end(), payload.begin(), payload.end());
	frame.push_back(Crc8Ccitt(frame));
	return frame;
}


// Records what the protocol transmits (ACKs), delivers (data callback) and reports (stream complete).
struct ProtocolRecorder {
	std::vector<std::vector<uint8>> transmitted;
	std::vector<std::vector<uint8>> delivered;
	std::vector<std::string> events; // "data" and "reset", in the order the protocol reported them
	std::vector<Protocol::StreamResult> streamResults;
	int streamCompleteCount = 0;
	int receiveResetCount = 0;
	int refuseTransmits = 0; // the next this many transmit calls return false (transport refused the frame)


	Protocol Create() {
		return Protocol(
			[this](std::span<const uint8> data) -> bool {
				transmitted.emplace_back(data.begin(), data.end());

				if (refuseTransmits > 0) {
					refuseTransmits--;
					return false;
				}

				return true;
			},
			[this](uint8 fragmentIndex, std::span<const uint8> data) -> bool {
				(void)fragmentIndex;
				delivered.emplace_back(data.begin(), data.end());
				events.push_back("data");
				return true;
			},
			[this](Protocol::StreamResult result) {
				streamResults.push_back(result);
				streamCompleteCount++;
			},
			[this]() {
				receiveResetCount++;
				events.push_back("reset");
			}
		);
	}
};


}


// A header announcing payloadSize > MAX_PAYLOAD_SIZE is dropped byte by byte; the next valid fragment is delivered.
TEST_CASE(T22_CspOversizedPayloadResyncs) {
	ProtocolRecorder recorder;
	Protocol protocol = recorder.Create();

	// Bad header (type 0, payloadSize 255, index 0). Its first byte is dropped; the remaining four bytes (FF 00 00 00) parse as one
	// harmless 4-byte ACK frame. Then a valid fragment carrying "ABCD".
	std::vector<uint8> stream = { 0x00, 0xFF, 0x00, 0x00, 0x00 };
	std::vector<uint8> payload = { 0x41, 0x42, 0x43, 0x44 };
	std::vector<uint8> valid = BuildFragment(CspDataFlags(0, true), 0x00, payload);
	stream.insert(stream.end(), valid.begin(), valid.end());

	protocol.DataReceived(stream);

	CHECK_EQ(recorder.delivered.size(), 1u);
	CHECK(recorder.delivered[0] == payload);
}


// A header claiming a payload above MAX_PAYLOAD_SIZE (16 for this protocol) with all bytes of the claimed packet present is
// rejected without copying it into the packet buffer; the valid fragment after it is delivered.
TEST_CASE(T22_CspPayloadAboveMaxResyncs) {
	ProtocolRecorder recorder;
	Protocol protocol = recorder.Create();

	// Bad header (type 0, payloadSize 20, index 0) followed by 21 bytes (20 payload + CRC) of 0xFF, which can not be a valid frame,
	// then a valid fragment carrying "ABCD". The whole stream fits the 80-byte receive buffer.
	std::vector<uint8> stream = { 0x00, 0x14, 0x00 };
	std::vector<uint8> garbage(21, 0xFF);
	stream.insert(stream.end(), garbage.begin(), garbage.end());

	std::vector<uint8> payload = { 0x41, 0x42, 0x43, 0x44 };
	std::vector<uint8> valid = BuildFragment(CspDataFlags(0, true), 0x00, payload);
	stream.insert(stream.end(), valid.begin(), valid.end());

	protocol.DataReceived(stream);

	CHECK_EQ(recorder.delivered.size(), 1u);
	CHECK(recorder.delivered[0] == payload);
}


// The same one-fragment message (id 1, index 0, last) arriving twice is a retransmission: delivered once, ACKed twice. The same
// payload as a message with id 2 is delivered again.
TEST_CASE(T36_CspRetransmitVsNewMessage) {
	ProtocolRecorder recorder;
	Protocol protocol = recorder.Create();

	std::vector<uint8> text = AsBytes("HELLO");
	std::vector<uint8> first = BuildFragment(CspDataFlags(1, true), 0x00, text);

	protocol.DataReceived(first);
	protocol.DataReceived(first);

	CHECK_EQ(recorder.delivered.size(), 1u);
	CHECK_EQ(recorder.transmitted.size(), 2u);
	CHECK(recorder.transmitted[0] == BuildFragment(CspAckFlags(1), 0x00, std::span<const uint8>()));
	CHECK(recorder.transmitted[1] == BuildFragment(CspAckFlags(1), 0x00, std::span<const uint8>()));

	protocol.DataReceived(BuildFragment(CspDataFlags(2, true), 0x00, text));

	CHECK_EQ(recorder.delivered.size(), 2u);
	CHECK_EQ(recorder.transmitted.size(), 3u);
}


// An empty data fragment is ACKed and otherwise ignored: no data callback, no stream-complete callback.
TEST_CASE(T24_EmptyFragmentAckedOnly) {
	ProtocolRecorder recorder;
	Protocol protocol = recorder.Create();

	std::vector<uint8> emptyFragment = BuildFragment(CspDataFlags(5, false), 0x00, std::span<const uint8>());
	protocol.DataReceived(emptyFragment);

	CHECK_EQ(recorder.delivered.size(), 0u);
	CHECK_EQ(recorder.streamCompleteCount, 0);
	CHECK_EQ(recorder.transmitted.size(), 1u);

	// The ACK echoes the message id (5) and the fragment index (0); the last bit is clear.
	const std::vector<uint8>& ack = recorder.transmitted[0];
	CHECK_EQ(ack[0], CspAckFlags(5));
	CHECK_EQ(ack[1], 0x00);
	CHECK_EQ(ack[2], 0x00);
}


// Send() accepts a message whose first transmit is refused; the fragment timer retransmits it, and the message completes once
// the receiver acknowledges it.
TEST_CASE(T31_CspSendAcceptedWhenFirstTransmitFails) {
	ResetSystem();
	ProtocolRecorder recorder;
	recorder.refuseTransmits = 1;
	Protocol protocol = recorder.Create();

	std::vector<uint8> message = AsBytes("HELLO");
	Protocol::SendResult result = protocol.Send(message);
	CHECK(result.status == Protocol::SendStatus::Success);
	CHECK(protocol.IsTransmitting());
	CHECK_EQ(recorder.transmitted.size(), 1u);

	// Within the fragment timeout (5 s) nothing is sent again.
	VhalSim::AdvanceMs(4000);
	protocol.Process();
	CHECK_EQ(recorder.transmitted.size(), 1u);

	// Past the timeout the refused fragment is transmitted again.
	VhalSim::AdvanceMs(1001);
	protocol.Process();
	CHECK_EQ(recorder.transmitted.size(), 2u);
	CHECK(recorder.transmitted[1] == recorder.transmitted[0]);

	// The id of the first message is taken from the wire; the frame is the one-fragment last fragment of that message.
	uint8 messageId = static_cast<uint8>(recorder.transmitted[0][0] >> 2);
	CHECK(recorder.transmitted[0] == BuildFragment(CspDataFlags(messageId, true), 0x00, message));

	// ACK for fragment 0 of that message completes the one-fragment message.
	protocol.DataReceived(BuildFragment(CspAckFlags(messageId), 0x00, std::span<const uint8>()));
	CHECK(!protocol.IsTransmitting());
	CHECK_EQ(recorder.streamCompleteCount, 1);
}


// A fragment sent at clock time 0 is timed out like any other; time 0 is not "no timer".
TEST_CASE(T32_CspTimerArmedAtTimeZero) {
	ResetSystem();
	VhalSim::SetMs(0);
	ProtocolRecorder recorder;
	recorder.refuseTransmits = 1;
	Protocol protocol = recorder.Create();

	std::vector<uint8> message = AsBytes("ZERO");
	Protocol::SendResult result = protocol.Send(message);
	CHECK(result.status == Protocol::SendStatus::Success);
	CHECK_EQ(recorder.transmitted.size(), 1u);

	VhalSim::SetMs(5001);
	protocol.Process();
	CHECK_EQ(recorder.transmitted.size(), 2u);
	// The retry is byte-identical to the refused original.
	CHECK(recorder.transmitted[1] == recorder.transmitted[0]);
}


// A frame with a bad CRC drops one byte at a time; a valid fragment right behind it is delivered in the same DataReceived call,
// without waiting for a retransmission.
TEST_CASE(T33_CspCrcFailureResyncsByteByByte) {
	ProtocolRecorder recorder;
	Protocol protocol = recorder.Create();

	// Header claims a 4-byte payload; the CRC byte is corrupted.
	std::vector<uint8> bad = BuildFragment(0x00, 0x00, AsBytes("BAD!"));
	bad.back() = (bad.back() != 0xEE) ? 0xEE : 0xED;

	std::vector<uint8> payload = AsBytes("GOOD");
	std::vector<uint8> stream = bad;
	std::vector<uint8> valid = BuildFragment(CspDataFlags(0, true), 0x00, payload);
	stream.insert(stream.end(), valid.begin(), valid.end());

	protocol.DataReceived(stream);

	CHECK_EQ(recorder.delivered.size(), 1u);
	CHECK(recorder.delivered[0] == payload);
}


// A frame whose length byte was corrupted upwards (claims 8 payload bytes, carries 4) must not swallow the start of the valid
// fragment behind it. Consuming the claimed length loses that fragment. The valid fragment is index 0 of its own message: under
// format v2 an index above 0 needs an open message.
TEST_CASE(T33_CspCorruptedLengthDoesNotEatNextFragment) {
	ProtocolRecorder recorder;
	Protocol protocol = recorder.Create();

	std::vector<uint8> bad = BuildFragment(0x00, 0x00, std::vector<uint8>(4, 0xFF));
	bad[1] = 8;

	std::vector<uint8> payload = AsBytes("GOOD");
	std::vector<uint8> stream = bad;
	std::vector<uint8> valid = BuildFragment(CspDataFlags(0, true), 0x00, payload);
	stream.insert(stream.end(), valid.begin(), valid.end());

	protocol.DataReceived(stream);

	CHECK_EQ(recorder.delivered.size(), 1u);
	if (recorder.delivered.size() == 1u) {
		CHECK(recorder.delivered[0] == payload);
	}
}


// A 40-byte message goes out as 16 + 16 + 8 byte fragments of one message id; only the last one has the last flag. The next
// message gets another id. An ACK for a received fragment echoes its message id and index.
TEST_CASE(T34_CspFragmentFlags) {
	ProtocolRecorder recorder;
	Protocol protocol = recorder.Create();

	std::vector<uint8> message(40);

	for (size_t i = 0; i < message.size(); i++) {
		message[i] = static_cast<uint8>(0x40 + i);
	}

	CHECK(protocol.Send(message).status == Protocol::SendStatus::Success);
	CHECK_EQ(recorder.transmitted.size(), 1u);
	uint8 messageId = static_cast<uint8>(recorder.transmitted[0][0] >> 2);

	// Each ACK for the fragment in flight makes the protocol send the next one; the third completes the message.
	for (uint8 index = 0; index < 3; index++) {
		protocol.DataReceived(BuildFragment(CspAckFlags(messageId), index, std::span<const uint8>()));
	}

	CHECK_EQ(recorder.streamCompleteCount, 1);
	CHECK_EQ(recorder.transmitted.size(), 3u);

	const size_t expectedSizes[3] = { 16, 16, 8 };
	std::vector<uint8> reassembled;

	for (size_t index = 0; index < 3; index++) {
		const std::vector<uint8>& frame = recorder.transmitted[index];
		CHECK_EQ(static_cast<uint8>(frame[0] >> 2), messageId);
		CHECK_EQ(static_cast<uint8>(frame[0] & kCspLastBit), static_cast<uint8>(index == 2 ? kCspLastBit : 0));
		CHECK_EQ(frame[2], static_cast<uint8>(index));
		CHECK_EQ(static_cast<size_t>(frame[1]), expectedSizes[index]);
		reassembled.insert(reassembled.end(), frame.begin() + 3, frame.end() - 1);
	}

	CHECK(reassembled == message);

	// The next message has a different id; it is a single fragment and carries the last flag.
	std::vector<uint8> next = AsBytes("NEXT");
	CHECK(protocol.Send(next).status == Protocol::SendStatus::Success);
	CHECK_EQ(recorder.transmitted.size(), 4u);
	CHECK(static_cast<uint8>(recorder.transmitted[3][0] >> 2) != messageId);
	CHECK_EQ(static_cast<uint8>(recorder.transmitted[3][0] & kCspLastBit), kCspLastBit);

	// An ACK sent for a received fragment echoes its message id (9) and index (0), with the last bit clear.
	ProtocolRecorder peerRecorder;
	Protocol peer = peerRecorder.Create();
	std::vector<uint8> piece = AsBytes("AB");
	peer.DataReceived(BuildFragment(CspDataFlags(9, false), 0x00, piece));

	CHECK_EQ(peerRecorder.transmitted.size(), 1u);
	CHECK(peerRecorder.transmitted[0] == BuildFragment(CspAckFlags(9), 0x00, std::span<const uint8>()));
}


// An ACK that carries the id of a finished message is stale and does not advance the message in progress. Only the ACK with the
// current message's id and fragment index does.
TEST_CASE(T35_CspStaleAckIgnored) {
	ResetSystem();
	ProtocolRecorder recorder;
	Protocol protocol = recorder.Create();

	std::vector<uint8> first = AsBytes("A");
	CHECK(protocol.Send(first).status == Protocol::SendStatus::Success);
	CHECK_EQ(recorder.transmitted.size(), 1u);
	uint8 firstId = static_cast<uint8>(recorder.transmitted[0][0] >> 2);

	protocol.DataReceived(BuildFragment(CspAckFlags(firstId), 0x00, std::span<const uint8>()));
	CHECK_EQ(recorder.streamCompleteCount, 1);

	// Message B has two fragments (16 + 4 bytes).
	std::vector<uint8> second = AsBytes("0123456789ABCDEFGHIJ");
	CHECK(protocol.Send(second).status == Protocol::SendStatus::Success);
	CHECK_EQ(recorder.transmitted.size(), 2u);
	uint8 secondId = static_cast<uint8>(recorder.transmitted[1][0] >> 2);
	CHECK(secondId != firstId);

	// A late ACK of message A for fragment 0 must not start fragment 1 of message B.
	protocol.DataReceived(BuildFragment(CspAckFlags(firstId), 0x00, std::span<const uint8>()));
	CHECK_EQ(recorder.transmitted.size(), 2u);
	CHECK(protocol.IsTransmitting());

	// The ACK for fragment 0 of message B does.
	protocol.DataReceived(BuildFragment(CspAckFlags(secondId), 0x00, std::span<const uint8>()));
	CHECK_EQ(recorder.transmitted.size(), 3u);
	CHECK_EQ(static_cast<uint8>(recorder.transmitted[2][0] >> 2), secondId);
	CHECK_EQ(recorder.transmitted[2][2], 1);
}


// A message starting at index 0 while the previous message lacks its last fragment resets the receiver once. Both payloads are
// delivered.
TEST_CASE(T37_CspIncompleteMessageResetsReceiver) {
	ProtocolRecorder recorder;
	Protocol protocol = recorder.Create();

	std::vector<uint8> firstPart = AsBytes("PART");
	std::vector<uint8> secondPart = AsBytes("WHOLE");

	protocol.DataReceived(BuildFragment(CspDataFlags(1, false), 0x00, firstPart));
	protocol.DataReceived(BuildFragment(CspDataFlags(2, true), 0x00, secondPart));

	CHECK_EQ(recorder.receiveResetCount, 1);
	CHECK_EQ(recorder.delivered.size(), 2u);
	CHECK(recorder.delivered[0] == firstPart);
	CHECK(recorder.delivered[1] == secondPart);

	std::vector<std::string> expectedEvents = { "data", "reset", "data" };
	CHECK(recorder.events == expectedEvents);
}


// A fragment that does not follow the open message is dropped: no ACK, no delivery, no state change.
TEST_CASE(T38_CspOutOfSequenceDropped) {
	{
		ProtocolRecorder recorder;
		Protocol protocol = recorder.Create();

		// Index 1 with no message open.
		protocol.DataReceived(BuildFragment(CspDataFlags(1, false), 0x01, AsBytes("XY")));

		CHECK_EQ(recorder.delivered.size(), 0u);
		CHECK_EQ(recorder.transmitted.size(), 0u);
	}

	{
		ProtocolRecorder recorder;
		Protocol protocol = recorder.Create();

		protocol.DataReceived(BuildFragment(CspDataFlags(1, false), 0x00, AsBytes("AB")));
		CHECK_EQ(recorder.delivered.size(), 1u);
		CHECK_EQ(recorder.transmitted.size(), 1u);

		// Index 2 skips index 1 of the open message: no ACK, not delivered.
		protocol.DataReceived(BuildFragment(CspDataFlags(1, true), 0x02, AsBytes("CD")));
		CHECK_EQ(recorder.delivered.size(), 1u);
		CHECK_EQ(recorder.transmitted.size(), 1u);

		// The message is still waiting for index 1, so index 1 with the last flag completes it.
		protocol.DataReceived(BuildFragment(CspDataFlags(1, true), 0x01, AsBytes("EF")));
		CHECK_EQ(recorder.delivered.size(), 2u);
		CHECK_EQ(recorder.transmitted.size(), 2u);
	}
}


// A peer that never ACKs makes the sender retransmit MAX_RETRIES (3) times, then give up. The stream completion reports Timeout
// once, and the sender stops transmitting.
TEST_CASE(T39_CspAbandonReported) {
	ResetSystem();
	ProtocolRecorder recorder;
	Protocol protocol = recorder.Create();

	std::vector<uint8> message = AsBytes("LOST");
	CHECK(protocol.Send(message).status == Protocol::SendStatus::Success);

	// The first transmission and three retries, each after the 5 s fragment timeout; the fourth timeout abandons.
	for (uint32 step = 0; step < 4; step++) {
		VhalSim::AdvanceMs(5001);
		protocol.Process();
	}

	CHECK_EQ(recorder.transmitted.size(), 4u);
	CHECK_EQ(recorder.streamCompleteCount, 1);
	CHECK(recorder.streamResults[0] == Protocol::StreamResult::Timeout);
	CHECK(!protocol.IsTransmitting());

	// Nothing is reported again later on.
	VhalSim::AdvanceMs(60000);
	protocol.Process();

	CHECK_EQ(recorder.streamCompleteCount, 1);
	CHECK_EQ(recorder.transmitted.size(), 4u);
}


int main() {
	TestRunner::RunAll();
	return VhalCheck::Finish("CompactStreaming");
}
