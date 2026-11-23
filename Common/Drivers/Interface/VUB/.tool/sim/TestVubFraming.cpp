#include "SimCheck.h"
#include "VubDoubles.h"
#include <Drivers/Interface/VUB/TestKit/VubContracts.h>
#include <Drivers/Interface/VUB/VubMeta.h>
#include <array>
#include <cstddef>
#include <span>


// The payload {01, 7E} by hand: the sum is 01 + 7E = 7F, the XOR is 01 XOR 7E = 7F. The byte 7E in the payload is sent as 7D 5E.
// The frame is 7E 01 7D 5E 7F 7F 7E.
void TestFrameByHand() {
	FlagFraming framing;
	VubMeta meta{};
	std::array<uint8, 2> payload = {0x01, 0x7E};
	std::array<uint8, 16> frame{};
	Result<std::size_t> framed = framing.Frame(meta, std::span<const uint8>(payload.data(), payload.size()), frame);
	SIM_EXPECT(framed.IsOk());
	SIM_EXPECT(framed.Value() == 7);
	const std::array<uint8, 7> expected = {0x7E, 0x01, 0x7D, 0x5E, 0x7F, 0x7F, 0x7E};
	for (std::size_t index = 0; index < expected.size(); index++) {
		SIM_EXPECT(frame[index] == expected[index]);
	}
}


// The same seven bytes pushed one at a time give one packet, the payload {01, 7E}, and nothing else.
void TestPushByHand() {
	FlagFraming framing;
	const std::array<uint8, 7> wire = {0x7E, 0x01, 0x7D, 0x5E, 0x7F, 0x7F, 0x7E};
	std::array<uint8, 16> out{};
	std::size_t length = 0;
	std::size_t packets = 0;
	for (uint8 byte : wire) {
		std::size_t current = 0;
		VubFrame state = framing.Push(byte, out, current);
		if (state == VubFrame::packet) {
			packets++;
			length = current;
		}
	}

	SIM_EXPECT(packets == 1);
	SIM_EXPECT(length == 2);
	SIM_EXPECT(out[0] == 0x01);
	SIM_EXPECT(out[1] == 0x7E);
}


// A frame with a changed byte is broken, and the next frame still comes out.
void TestResync() {
	FlagFraming framing;
	const std::array<uint8, 7> damaged = {0x7E, 0x01, 0x7D, 0x5E, 0x7F, 0x70, 0x7E};
	const std::array<uint8, 7> clean = {0x7E, 0x01, 0x7D, 0x5E, 0x7F, 0x7F, 0x7E};
	std::array<uint8, 16> out{};
	std::size_t packets = 0;
	std::size_t broken = 0;
	for (uint8 byte : damaged) {
		std::size_t current = 0;
		VubFrame state = framing.Push(byte, out, current);
		if (state == VubFrame::broken) {
			broken++;
		}
	}

	for (uint8 byte : clean) {
		std::size_t current = 0;
		VubFrame state = framing.Push(byte, out, current);
		if (state == VubFrame::packet) {
			packets++;
		}
	}

	SIM_EXPECT(broken == 1);
	SIM_EXPECT(packets == 1);
}


// The framing contract, with no loss and with loss: every packet that comes out is one sent, in order, and the frames after a lossy
// part come out again.
void TestFramingContract() {
	FlagFraming framing;
	SIM_EXPECT(VubCheckFraming(framing, 3, 200, 32, 0));
	SIM_EXPECT(VubCheckFraming(framing, 5, 200, 32, 10));
	SIM_EXPECT(VubCheckFraming(framing, 8, 200, 32, 25));
}


// The framing that declares a growth of length, while FlagFraming adds 2 flags and 2 check bytes to every packet, and escapes. The
// stack sizes the framed buffer from Growth, so a framing that adds more than it declares must fail the contract.
struct LyingGrowthFraming : FlagFraming {
	static constexpr std::size_t Growth(std::size_t length) {
		return length;
	}
};


void TestLyingGrowthFailsContract() {
	LyingGrowthFraming framing;
	SIM_EXPECT(!VubCheckFraming(framing, 3, 200, 32, 0));
}


int main() {
	TestFrameByHand();
	TestPushByHand();
	TestResync();
	TestFramingContract();
	TestLyingGrowthFailsContract();
	return SimFinish("TestVubFraming");
}
