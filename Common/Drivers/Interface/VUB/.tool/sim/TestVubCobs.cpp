#include "SimCheck.h"
#include <Drivers/Interface/VUB/Framing/Cobs.h>
#include <Drivers/Interface/VUB/TestKit/Contracts.h>
#include <Drivers/Interface/VUB/Meta.h>
#include <array>
#include <cstddef>
#include <span>


// The expected wire bytes below are written by hand from the COBS rules: a code byte gives the distance to the next zero, the zero
// itself is not sent, a code of 255 means 254 bytes with no zero after them, and every frame ends with 00.


// Encodes payload and compares the result with the expected wire bytes.
template<std::size_t Expected, std::size_t Payload>
void ExpectFrame(const std::array<uint8, Payload>& payload, const std::array<uint8, Expected>& expected) {
	VUB::Cobs framing;
	VUB::Meta meta{};
	std::array<uint8, 300> frame{};
	Result<std::size_t> framed = framing.Frame(meta, std::span<const uint8>(payload.data(), payload.size()), frame);
	SIM_EXPECT(framed.IsOk());
	SIM_EXPECT(framed.Value() == Expected);
	for (std::size_t index = 0; index < Expected && index < framed.Value(); index++) {
		SIM_EXPECT(frame[index] == expected[index]);
	}
}


// 00 -> 01 01 00: two empty blocks, one at the zero and one at the end.
void TestZeroByHand() {
	const std::array<uint8, 1> payload = {0x00};
	const std::array<uint8, 3> expected = {0x01, 0x01, 0x00};
	ExpectFrame(payload, expected);
}


// 00 00 -> 01 01 01 00.
void TestTwoZerosByHand() {
	const std::array<uint8, 2> payload = {0x00, 0x00};
	const std::array<uint8, 4> expected = {0x01, 0x01, 0x01, 0x00};
	ExpectFrame(payload, expected);
}


// 11 22 00 33 -> 03 11 22 02 33 00: the code 03 covers 11 22, the code 02 covers 33.
void TestMixedByHand() {
	const std::array<uint8, 4> payload = {0x11, 0x22, 0x00, 0x33};
	const std::array<uint8, 6> expected = {0x03, 0x11, 0x22, 0x02, 0x33, 0x00};
	ExpectFrame(payload, expected);
}


// 11 00 00 00 -> 02 11 01 01 01 00.
void TestRunOfZerosByHand() {
	const std::array<uint8, 4> payload = {0x11, 0x00, 0x00, 0x00};
	const std::array<uint8, 6> expected = {0x02, 0x11, 0x01, 0x01, 0x01, 0x00};
	ExpectFrame(payload, expected);
}


// 254 non-zero bytes 01 .. FE -> FF, the 254 bytes, 00. The code 255 has no zero after it, so no second code byte follows.
void TestFull254ByHand() {
	std::array<uint8, 254> payload{};
	std::array<uint8, 256> expected{};
	expected[0] = 0xFF;
	for (std::size_t index = 0; index < 254; index++) {
		payload[index] = static_cast<uint8>(index + 1);
		expected[index + 1] = static_cast<uint8>(index + 1);
	}

	expected[255] = 0x00;
	ExpectFrame(payload, expected);
}


// 255 non-zero bytes 01 .. FE, 01 -> FF, 01 .. FE, 02, 01, 00: the 255th byte starts a block with code 02.
void TestFull255ByHand() {
	std::array<uint8, 255> payload{};
	std::array<uint8, 258> expected{};
	expected[0] = 0xFF;
	for (std::size_t index = 0; index < 255; index++) {
		payload[index] = static_cast<uint8>(index % 254 + 1);
	}

	for (std::size_t index = 0; index < 254; index++) {
		expected[index + 1] = static_cast<uint8>(index + 1);
	}

	expected[255] = 0x02;
	expected[256] = 0x01;
	expected[257] = 0x00;
	ExpectFrame(payload, expected);
}


// A frame of the wire, pushed a byte at a time, gives the packet again: 03 11 22 02 33 00 -> 11 22 00 33.
void TestDecodeByHand() {
	VUB::Cobs framing;
	const std::array<uint8, 6> wire = {0x03, 0x11, 0x22, 0x02, 0x33, 0x00};
	std::array<uint8, 16> out{};
	std::size_t packets = 0;
	std::size_t length = 0;
	for (uint8 byte : wire) {
		std::size_t current = 0;
		VUB::FrameStatus state = framing.Push(byte, out, current);
		if (state == VUB::FrameStatus::packet) {
			packets++;
			length = current;
		}
	}

	SIM_EXPECT(packets == 1);
	SIM_EXPECT(length == 4);
	SIM_EXPECT(out[0] == 0x11);
	SIM_EXPECT(out[1] == 0x22);
	SIM_EXPECT(out[2] == 0x00);
	SIM_EXPECT(out[3] == 0x33);
}


// The 255 byte frame decodes back: the code 255 gives no zero, the code 02 gives the 255th byte, and the zero after it is not
// appended because the frame ends there.
void TestDecodeFull255() {
	VUB::Cobs framing;
	std::array<uint8, 255> payload{};
	for (std::size_t index = 0; index < 255; index++) {
		payload[index] = static_cast<uint8>(index % 254 + 1);
	}

	VUB::Meta meta{};
	std::array<uint8, 300> frame{};
	Result<std::size_t> framed = framing.Frame(meta, std::span<const uint8>(payload.data(), payload.size()), frame);
	SIM_EXPECT(framed.IsOk());
	std::array<uint8, 300> out{};
	std::size_t packets = 0;
	std::size_t length = 0;
	bool same = true;
	for (std::size_t index = 0; index < framed.Value(); index++) {
		std::size_t current = 0;
		VUB::FrameStatus state = framing.Push(frame[index], out, current);
		if (state == VUB::FrameStatus::packet) {
			packets++;
			length = current;
			for (std::size_t byteIndex = 0; byteIndex < current; byteIndex++) {
				same = same && out[byteIndex] == payload[byteIndex];
			}
		}
	}

	SIM_EXPECT(packets == 1);
	SIM_EXPECT(length == 255);
	SIM_EXPECT(same);
}


// A frame whose block is cut short by a zero is broken: 03 11 00 has a block of two data bytes with one. The next frame, 02 11 00,
// comes out after it.
void TestBrokenThenResync() {
	VUB::Cobs framing;
	const std::array<uint8, 3> damaged = {0x03, 0x11, 0x00};
	const std::array<uint8, 3> clean = {0x02, 0x11, 0x00};
	std::array<uint8, 16> out{};
	std::size_t broken = 0;
	std::size_t packets = 0;
	std::size_t length = 0;
	for (uint8 byte : damaged) {
		std::size_t current = 0;
		if (framing.Push(byte, out, current) == VUB::FrameStatus::broken) {
			broken++;
		}
	}

	for (uint8 byte : clean) {
		std::size_t current = 0;
		if (framing.Push(byte, out, current) == VUB::FrameStatus::packet) {
			packets++;
			length = current;
		}
	}

	SIM_EXPECT(broken == 1);
	SIM_EXPECT(packets == 1);
	SIM_EXPECT(length == 1);
	SIM_EXPECT(out[0] == 0x11);
}


// A frame with more bytes than the output holds is tooLong at its delimiter, and the next frame still comes out.
void TestTooLongThenResync() {
	VUB::Cobs framing;
	const std::array<uint8, 6> first = {0x05, 0x11, 0x22, 0x33, 0x44, 0x00};
	const std::array<uint8, 3> second = {0x02, 0x55, 0x00};
	std::array<uint8, 2> out{};
	std::size_t tooLong = 0;
	std::size_t packets = 0;
	std::size_t length = 0;
	for (uint8 byte : first) {
		std::size_t current = 0;
		if (framing.Push(byte, out, current) == VUB::FrameStatus::tooLong) {
			tooLong++;
		}
	}

	for (uint8 byte : second) {
		std::size_t current = 0;
		if (framing.Push(byte, out, current) == VUB::FrameStatus::packet) {
			packets++;
			length = current;
		}
	}

	SIM_EXPECT(tooLong == 1);
	SIM_EXPECT(packets == 1);
	SIM_EXPECT(length == 1);
	SIM_EXPECT(out[0] == 0x55);
}


// Reset in the middle of a frame drops the partial frame: the bytes after it start a frame.
void TestResetDropsPartialFrame() {
	VUB::Cobs framing;
	std::array<uint8, 16> out{};
	const std::array<uint8, 2> partial = {0x03, 0x11};
	for (uint8 byte : partial) {
		std::size_t current = 0;
		framing.Push(byte, out, current);
	}

	framing.Reset();
	const std::array<uint8, 3> next = {0x02, 0x66, 0x00};
	std::size_t packets = 0;
	std::size_t length = 0;
	for (uint8 byte : next) {
		std::size_t current = 0;
		if (framing.Push(byte, out, current) == VUB::FrameStatus::packet) {
			packets++;
			length = current;
		}
	}

	SIM_EXPECT(packets == 1);
	SIM_EXPECT(length == 1);
	SIM_EXPECT(out[0] == 0x66);
}


// Growth is the COBS worst case plus the delimiter: n + n / 254 + 2. The frames above fit it: 254 bytes give 256, 255 give 258.
void TestGrowth() {
	SIM_EXPECT(VUB::Cobs::Growth(254) == 257);
	SIM_EXPECT(VUB::Cobs::Growth(255) == 258);
	SIM_EXPECT(VUB::Cobs::Growth(0) == 2);
	SIM_EXPECT(VUB::Cobs::Growth(1) == 3);
}


// The leading delimiter adds one byte in front of each frame: 11 -> 00 02 11 00.
void TestLeadingDelimiterByHand() {
	VUB::CobsFlushed framing;
	VUB::Meta meta{};
	const std::array<uint8, 1> payload = {0x11};
	std::array<uint8, 8> frame{};
	Result<std::size_t> framed = framing.Frame(meta, std::span<const uint8>(payload.data(), payload.size()), frame);
	SIM_EXPECT(framed.IsOk());
	SIM_EXPECT(framed.Value() == 4);
	const std::array<uint8, 4> expected = {0x00, 0x02, 0x11, 0x00};
	for (std::size_t index = 0; index < expected.size(); index++) {
		SIM_EXPECT(frame[index] == expected[index]);
	}

	SIM_EXPECT(VUB::CobsFlushed::Growth(1) == 4);
}


// An empty packet is refused: the frame would carry no payload.
void TestEmptyRefused() {
	VUB::Cobs framing;
	VUB::Meta meta{};
	std::array<uint8, 8> frame{};
	SIM_EXPECT(framing.Frame(meta, std::span<const uint8>(), frame).IsErr());
}


// The framing contract with no loss: every packet comes out exactly, with random chunking of the frame's bytes.
void TestFramingContractNoLoss() {
	VUB::Cobs framing;
	SIM_EXPECT(VUB::CheckFraming(framing, 3, 200, 32, 0));
	VUB::CobsFlushed flushed;
	SIM_EXPECT(VUB::CheckFraming(flushed, 4, 200, 32, 0));
}


// With loss, COBS has no check of its own, so a damaged frame can come out as a different packet. The framing still resynchronises:
// after the lossy part, the clean frames come out exactly.
void TestResyncContractWithLoss() {
	VUB::Cobs framing;
	SIM_EXPECT(VUB::CheckResync(framing, 5, 200, 32, 10));
	SIM_EXPECT(VUB::CheckResync(framing, 8, 200, 32, 25));
}


int main() {
	TestZeroByHand();
	TestTwoZerosByHand();
	TestMixedByHand();
	TestRunOfZerosByHand();
	TestFull254ByHand();
	TestFull255ByHand();
	TestDecodeByHand();
	TestDecodeFull255();
	TestBrokenThenResync();
	TestTooLongThenResync();
	TestResetDropsPartialFrame();
	TestGrowth();
	TestLeadingDelimiterByHand();
	TestEmptyRefused();
	TestFramingContractNoLoss();
	TestResyncContractWithLoss();
	return SimFinish("TestVubCobs");
}
