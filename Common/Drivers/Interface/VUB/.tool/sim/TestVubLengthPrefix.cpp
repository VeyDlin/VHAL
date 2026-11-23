#include "SimCheck.h"
#include <Drivers/Interface/VUB/Framing/VubLengthPrefix.h>
#include <Drivers/Interface/VUB/TestKit/VubContracts.h>
#include <Drivers/Interface/VUB/VubMeta.h>
#include <array>
#include <cstddef>
#include <span>


// The expected wire bytes below are written by hand from the layout: a sync byte A5, the payload length (one byte, or two bytes little-
// endian), a header check byte, then the payload. The check byte makes the sum of the sync, the length and itself 00 modulo 256.


// Frames the payload and compares the result with the expected wire bytes.
template<typename Framing, std::size_t Payload, std::size_t Expected>
void ExpectFrame(const std::array<uint8, Payload>& payload, const std::array<uint8, Expected>& expected) {
	Framing framing;
	VubMeta meta{};
	std::array<uint8, 300> frame{};
	Result<std::size_t> framed = framing.Frame(meta, std::span<const uint8>(payload.data(), payload.size()), frame);
	SIM_EXPECT(framed.IsOk());
	SIM_EXPECT(framed.Value() == Expected);
	for (std::size_t index = 0; index < Expected && index < framed.Value(); index++) {
		SIM_EXPECT(frame[index] == expected[index]);
	}
}


// Pushes the bytes and returns the number of packets that came out. The last packet's length and first byte are in the out values.
template<typename Framing, std::size_t Count>
std::size_t PushAll(Framing& framing, const std::array<uint8, Count>& wire, std::size_t& length, uint8& first, std::size_t& broken) {
	std::array<uint8, 300> out{};
	std::size_t packets = 0;
	for (uint8 byte : wire) {
		std::size_t current = 0;
		VubFrame state = framing.Push(byte, out, current);
		if (state == VubFrame::packet) {
			packets++;
			length = current;
			first = out[0];
		} else if (state == VubFrame::broken) {
			broken++;
		}
	}

	return packets;
}


// One byte length: 11 22 33 -> A5 03 58 11 22 33. The sum A5 + 03 + 58 is 256, so 58 is the check.
void TestOneByteByHand() {
	const std::array<uint8, 3> payload = {0x11, 0x22, 0x33};
	const std::array<uint8, 6> expected = {0xA5, 0x03, 0x58, 0x11, 0x22, 0x33};
	ExpectFrame<VubLengthPrefix<1>>(payload, expected);
}


// Two byte length, little-endian: 11 22 33 -> A5 03 00 58 11 22 33.
void TestTwoByteByHand() {
	const std::array<uint8, 3> payload = {0x11, 0x22, 0x33};
	const std::array<uint8, 7> expected = {0xA5, 0x03, 0x00, 0x58, 0x11, 0x22, 0x33};
	ExpectFrame<VubLengthPrefix<2>>(payload, expected);
}


// A 256 byte payload with two byte length: the length is 00 01, and A5 + 00 + 01 is A6, so the check is 5A (A6 + 5A is 100 in hex,
// which is 256).
void TestTwoByteLongByHand() {
	std::array<uint8, 256> payload{};
	for (std::size_t index = 0; index < payload.size(); index++) {
		payload[index] = static_cast<uint8>(index);
	}

	std::array<uint8, 260> expected{};
	expected[0] = 0xA5;
	expected[1] = 0x00;
	expected[2] = 0x01;
	expected[3] = 0x5A;
	for (std::size_t index = 0; index < payload.size(); index++) {
		expected[4 + index] = payload[index];
	}

	ExpectFrame<VubLengthPrefix<2>>(payload, expected);
}


// The worked example of the wiki page: the packet 11 22 33 44 55 with one byte length. The sum A5 + 05 is AA, so the check is 56 (AA + 56
// is 100 in hexadecimal, which is 256). The frame is A5 05 56 11 22 33 44 55, and it is read back as the five payload bytes.
void TestWorkedExampleOneByte() {
	const std::array<uint8, 5> payload = {0x11, 0x22, 0x33, 0x44, 0x55};
	const std::array<uint8, 8> expected = {0xA5, 0x05, 0x56, 0x11, 0x22, 0x33, 0x44, 0x55};
	ExpectFrame<VubLengthPrefix<1>>(payload, expected);

	VubLengthPrefix<1> framing;
	std::array<uint8, 16> out{};
	std::size_t length = 0;
	for (std::size_t index = 0; index + 1 < expected.size(); index++) {
		SIM_EXPECT(framing.Push(expected[index], out, length) == VubFrame::pending);
	}

	SIM_EXPECT(framing.Push(expected[expected.size() - 1], out, length) == VubFrame::packet);
	SIM_EXPECT(length == 5);
	for (std::size_t index = 0; index < payload.size(); index++) {
		SIM_EXPECT(out[index] == payload[index]);
	}
}


// The header check sums to 00: sync, length bytes and check add up to 0 modulo 256, for each of the frames above.
void TestHeaderSumsToZero() {
	SIM_EXPECT(static_cast<uint8>(0xA5 + 0x03 + 0x58) == 0x00);
	SIM_EXPECT(static_cast<uint8>(0xA5 + 0x03 + 0x00 + 0x58) == 0x00);
}


// Pushed by hand: A5 03 58 11 22 33 gives one packet, 11 22 33.
void TestDecodeByHand() {
	VubLengthPrefix<1> framing;
	const std::array<uint8, 6> wire = {0xA5, 0x03, 0x58, 0x11, 0x22, 0x33};
	std::size_t length = 0;
	uint8 first = 0;
	std::size_t broken = 0;
	std::size_t packets = PushAll(framing, wire, length, first, broken);
	SIM_EXPECT(packets == 1);
	SIM_EXPECT(length == 3);
	SIM_EXPECT(first == 0x11);
	SIM_EXPECT(broken == 0);
}


// A header whose check fails: the sync byte is dropped and the bytes after it are read again for a sync. A5 03 59 is bad (the check
// is 58), so its payload 11 22 33 is not read as a frame. The good frame A5 03 58 after it is read, and its payload 44 55 66 comes out.
void TestBadHeaderSlides() {
	VubLengthPrefix<1> framing;
	const std::array<uint8, 12> wire = {0xA5, 0x03, 0x59, 0x11, 0x22, 0x33, 0xA5, 0x03, 0x58, 0x44, 0x55, 0x66};
	std::size_t length = 0;
	uint8 first = 0;
	std::size_t broken = 0;
	std::size_t packets = PushAll(framing, wire, length, first, broken);
	SIM_EXPECT(packets == 1);
	SIM_EXPECT(length == 3);
	SIM_EXPECT(first == 0x44);
	SIM_EXPECT(broken == 0);
}


// A sync byte inside a bad header starts the header that follows it: A5 A5 03 58 11 22 33 is the frame A5 03 58 11 22 33 with a stray
// A5 in front, and it comes out whole.
void TestStraySyncInHeader() {
	VubLengthPrefix<1> framing;
	const std::array<uint8, 7> wire = {0xA5, 0xA5, 0x03, 0x58, 0x11, 0x22, 0x33};
	std::size_t length = 0;
	uint8 first = 0;
	std::size_t broken = 0;
	std::size_t packets = PushAll(framing, wire, length, first, broken);
	SIM_EXPECT(packets == 1);
	SIM_EXPECT(length == 3);
	SIM_EXPECT(first == 0x11);
}


// A lost payload byte, on a line that cannot report its state: the frame A5 03 58 11 22 33 arrives as 11 33 and the next frame's sync
// byte is taken as payload. The packet 11 33 A5 comes out. The rest of the next frame (03 58 AA BB CC) is read as a broken start and
// dropped. The frame after it comes out whole.
void TestLostPayloadByteLosesNextFrame() {
	VubLengthPrefix<1> framing;
	const std::array<uint8, 6> damaged = {0xA5, 0x03, 0x58, 0x11, 0x33, 0xA5};
	const std::array<uint8, 5> lost = {0x03, 0x58, 0xAA, 0xBB, 0xCC};
	const std::array<uint8, 6> clean = {0xA5, 0x03, 0x58, 0x77, 0x88, 0x99};
	std::size_t length = 0;
	uint8 first = 0;
	std::size_t broken = 0;
	std::size_t packets = PushAll(framing, damaged, length, first, broken);
	SIM_EXPECT(packets == 1);
	SIM_EXPECT(length == 3);
	SIM_EXPECT(first == 0x11);
	packets = PushAll(framing, lost, length, first, broken);
	SIM_EXPECT(packets == 0);
	packets = PushAll(framing, clean, length, first, broken);
	SIM_EXPECT(packets == 1);
	SIM_EXPECT(length == 3);
	SIM_EXPECT(first == 0x77);
}


// A header with length zero is not a header: A5 00 5B has the check of 00 modulo 256, and it is refused. The sync byte after it is the
// start of the frame A5 03 58 (sum 00), so 77 88 99 comes out, and the zero length header gives no packet.
void TestZeroLengthHeaderRefused() {
	VubLengthPrefix<1> framing;
	const std::array<uint8, 9> wire = {0xA5, 0x00, 0x5B, 0xA5, 0x03, 0x58, 0x77, 0x88, 0x99};
	std::size_t length = 0;
	uint8 first = 0;
	std::size_t broken = 0;
	std::size_t packets = PushAll(framing, wire, length, first, broken);
	SIM_EXPECT(packets == 1);
	SIM_EXPECT(length == 3);
	SIM_EXPECT(first == 0x77);
}


// A header whose check fails drops its sync byte, and the bytes after it are read again for a sync. A5 03 11 has the sum B9, not 00, so
// it is no header. Its bytes 03 and 11 hold no sync, and the sync byte after them starts A5 03 58 (sum 00): 77 88 99 comes out.
void TestBadHeaderThenSync() {
	VubLengthPrefix<1> framing;
	const std::array<uint8, 9> wire = {0xA5, 0x03, 0x11, 0xA5, 0x03, 0x58, 0x77, 0x88, 0x99};
	std::size_t length = 0;
	uint8 first = 0;
	std::size_t broken = 0;
	std::size_t packets = PushAll(framing, wire, length, first, broken);
	SIM_EXPECT(packets == 1);
	SIM_EXPECT(length == 3);
	SIM_EXPECT(first == 0x77);
}


// A failed header that holds a sync byte after its first byte keeps that sync byte. A5 03 A5 has the sum 4D, not 00: the window slides
// past A5 03 and reads A5 as the start of the next header. A5 03 58 (sum 00) follows, so 77 88 99 comes out. A reader that clears the
// window at the first byte that is no sync drops the A5 and gives no packet.
void TestBadHeaderKeepsSyncInWindow() {
	VubLengthPrefix<1> framing;
	const std::array<uint8, 8> wire = {0xA5, 0x03, 0xA5, 0x03, 0x58, 0x77, 0x88, 0x99};
	std::size_t length = 0;
	uint8 first = 0;
	std::size_t broken = 0;
	std::size_t packets = PushAll(framing, wire, length, first, broken);
	SIM_EXPECT(packets == 1);
	SIM_EXPECT(length == 3);
	SIM_EXPECT(first == 0x77);
}


// Reset in the middle of a frame: the partial frame is dropped, and the clean frame that follows comes out.
void TestResetDropsPartialFrame() {
	VubLengthPrefix<1> framing;
	const std::array<uint8, 4> partial = {0xA5, 0x03, 0x58, 0x11};
	const std::array<uint8, 5> clean = {0xA5, 0x02, 0x59, 0x66, 0x77};
	std::size_t length = 0;
	uint8 first = 0;
	std::size_t broken = 0;
	PushAll(framing, partial, length, first, broken);
	framing.Reset();
	std::size_t packets = PushAll(framing, clean, length, first, broken);
	SIM_EXPECT(packets == 1);
	SIM_EXPECT(length == 2);
	SIM_EXPECT(first == 0x66);
}


// A zero length packet is refused, and so is a packet longer than the length field holds.
void TestRefusals() {
	VubLengthPrefix<1> one;
	VubMeta meta{};
	std::array<uint8, 300> frame{};
	std::array<uint8, 1> none{};
	SIM_EXPECT(one.Frame(meta, std::span<const uint8>(none.data(), 0), frame).IsErr());
	std::array<uint8, 256> big{};
	SIM_EXPECT(one.Frame(meta, std::span<const uint8>(big.data(), big.size()), frame).IsErr());
}


// Growth is the header: sync, length bytes and check. One byte length adds 3, two byte length adds 4, whatever the payload.
void TestGrowth() {
	SIM_EXPECT(VubLengthPrefix<1>::Growth(1) == 3);
	SIM_EXPECT(VubLengthPrefix<1>::Growth(255) == 3);
	SIM_EXPECT(VubLengthPrefix<2>::Growth(1) == 4);
	SIM_EXPECT(VubLengthPrefix<2>::Growth(1000) == 4);
}


// The framing contract with no loss, and with loss: the frames after a lossy part come out exactly.
void TestFramingContract() {
	VubLengthPrefix<1> one;
	SIM_EXPECT(VubCheckFraming(one, 3, 200, 32, 0));
	VubLengthPrefix<2> two;
	SIM_EXPECT(VubCheckFraming(two, 4, 200, 32, 0));
}


// With loss, a damaged frame can come out as a different packet: the payload has no check, and the integrity is the CRC transform's
// job. The framing still resynchronises: the clean frames come out exactly.
void TestResyncContractWithLoss() {
	VubLengthPrefix<1> one;
	SIM_EXPECT(VubCheckResync(one, 5, 200, 32, 10));
	VubLengthPrefix<2> two;
	SIM_EXPECT(VubCheckResync(two, 6, 200, 32, 25));
}


int main() {
	TestOneByteByHand();
	TestTwoByteByHand();
	TestTwoByteLongByHand();
	TestWorkedExampleOneByte();
	TestHeaderSumsToZero();
	TestDecodeByHand();
	TestBadHeaderSlides();
	TestStraySyncInHeader();
	TestLostPayloadByteLosesNextFrame();
	TestZeroLengthHeaderRefused();
	TestBadHeaderThenSync();
	TestBadHeaderKeepsSyncInWindow();
	TestResetDropsPartialFrame();
	TestRefusals();
	TestGrowth();
	TestFramingContract();
	TestResyncContractWithLoss();
	return SimFinish("TestVubLengthPrefix");
}
