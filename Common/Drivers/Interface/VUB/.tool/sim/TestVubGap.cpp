#include "SimCheck.h"
#include <Drivers/Interface/VUB/Framing/VubGap.h>
#include <Drivers/Interface/VUB/TestKit/VubContracts.h>
#include <Drivers/Interface/VUB/VubMeta.h>
#include <array>
#include <cstddef>
#include <span>


// The gap framing puts no bytes on the wire: a frame is the payload, and the silence between two frames is the boundary. The times below
// are microseconds. The gap is 1000 us unless a test says otherwise.


// The options of a gap of 1000 us, with no baud rate: the send side waits for the gap, and assumes nothing about the line's speed.
VubGap::Options GapOptions(uint32 gapUs) {
	VubGap::Options options;
	options.gapUs = gapUs;
	options.timeoutUs = 5000;
	return options;
}


// The payload goes out unchanged.
void TestFrameIsThePayload() {
	VubGap framing(GapOptions(1000));
	VubMeta meta{};
	const std::array<uint8, 3> payload = {0x11, 0x22, 0x00};
	std::array<uint8, 8> frame{};
	Result<std::size_t> framed = framing.Frame(meta, std::span<const uint8>(payload.data(), payload.size()), frame);
	SIM_EXPECT(framed.IsOk());
	SIM_EXPECT(framed.Value() == 3);
	SIM_EXPECT(frame[0] == 0x11);
	SIM_EXPECT(frame[1] == 0x22);
	SIM_EXPECT(frame[2] == 0x00);
	SIM_EXPECT(VubGap::Growth(3) == 0);
	SIM_EXPECT(VubGap::usesTime);
}


// The bytes 11 at 0, 22 at 100 and 33 at 1099 are one frame: no silence reaches the gap of 1000. It comes out at 1099 + 1000 = 2099,
// and not a microsecond earlier.
void TestFrameEndsAtSilence() {
	VubGap framing(GapOptions(1000));
	std::array<uint8, 16> out{};
	std::size_t length = 0;
	SIM_EXPECT(framing.Push(0x11, 0, out, length) == VubFrame::pending);
	SIM_EXPECT(framing.Push(0x22, 100, out, length) == VubFrame::pending);
	SIM_EXPECT(framing.Push(0x33, 1099, out, length) == VubFrame::pending);
	SIM_EXPECT(framing.Poll(2098, out, length) == VubFrame::pending);
	SIM_EXPECT(framing.Poll(2099, out, length) == VubFrame::packet);
	SIM_EXPECT(length == 3);
	SIM_EXPECT(out[0] == 0x11);
	SIM_EXPECT(out[1] == 0x22);
	SIM_EXPECT(out[2] == 0x33);
	SIM_EXPECT(framing.Poll(5000, out, length) == VubFrame::pending);
}


// A byte that comes after the gap starts the next frame. 11 at 0 and 22 at 2000: the frame {11} comes out with the byte 22, which is kept
// for the next frame. 33 at 2100 joins 22, and {22 33} comes out at 3100.
void TestByteAfterGapStartsNextFrame() {
	VubGap framing(GapOptions(1000));
	std::array<uint8, 16> out{};
	std::size_t length = 0;
	SIM_EXPECT(framing.Push(0x11, 0, out, length) == VubFrame::pending);
	SIM_EXPECT(framing.Push(0x22, 2000, out, length) == VubFrame::packet);
	SIM_EXPECT(length == 1);
	SIM_EXPECT(out[0] == 0x11);
	SIM_EXPECT(framing.Push(0x33, 2100, out, length) == VubFrame::pending);
	SIM_EXPECT(framing.Poll(3099, out, length) == VubFrame::pending);
	SIM_EXPECT(framing.Poll(3100, out, length) == VubFrame::packet);
	SIM_EXPECT(length == 2);
	SIM_EXPECT(out[0] == 0x22);
	SIM_EXPECT(out[1] == 0x33);
}


// A byte that comes after the gap, with the next byte in the same chunk, leaves no frame lost: the Push that ends a frame gives the packet
// and the byte that starts the next one is not counted in it.
void TestGapEndsOnlyAtSilence() {
	VubGap framing(GapOptions(1000));
	std::array<uint8, 16> out{};
	std::size_t length = 0;
	SIM_EXPECT(framing.Push(0x11, 0, out, length) == VubFrame::pending);
	SIM_EXPECT(framing.Push(0x22, 999, out, length) == VubFrame::pending);
	SIM_EXPECT(framing.Push(0x33, 1998, out, length) == VubFrame::pending);
	SIM_EXPECT(framing.Poll(2998, out, length) == VubFrame::packet);
	SIM_EXPECT(length == 3);
}


// Reset drops the partial frame: 11 at 0 and 22 at 100 are dropped, and 33 at 200 is the start of the next frame.
void TestResetDropsPartialFrame() {
	VubGap framing(GapOptions(1000));
	std::array<uint8, 16> out{};
	std::size_t length = 0;
	SIM_EXPECT(framing.Push(0x11, 0, out, length) == VubFrame::pending);
	SIM_EXPECT(framing.Push(0x22, 100, out, length) == VubFrame::pending);
	framing.Reset();
	SIM_EXPECT(framing.Push(0x33, 200, out, length) == VubFrame::pending);
	SIM_EXPECT(framing.Poll(1200, out, length) == VubFrame::packet);
	SIM_EXPECT(length == 1);
	SIM_EXPECT(out[0] == 0x33);
}


// A frame longer than the output is tooLong when its silence ends it; the next frame comes out whole.
void TestTooLong() {
	VubGap framing(GapOptions(1000));
	std::array<uint8, 2> out{};
	std::size_t length = 0;
	SIM_EXPECT(framing.Push(0x11, 0, out, length) == VubFrame::pending);
	SIM_EXPECT(framing.Push(0x22, 10, out, length) == VubFrame::pending);
	SIM_EXPECT(framing.Push(0x33, 20, out, length) == VubFrame::pending);
	SIM_EXPECT(framing.Poll(1020, out, length) == VubFrame::tooLong);
	SIM_EXPECT(framing.Push(0x44, 2000, out, length) == VubFrame::pending);
	SIM_EXPECT(framing.Poll(3000, out, length) == VubFrame::packet);
	SIM_EXPECT(length == 1);
	SIM_EXPECT(out[0] == 0x44);
}


// Characters: 3.5 character times at 9600 baud, 11 bits each, is 38.5 bits. One bit is 1000000 / 9600 us, so 38.5 bits are
// 38500000 / 9600 = 4010.42 us. Rounded up, the gap is 4011 us. The gap ends at 4011 and not before.
void TestCharacterGapAtBaud() {
	VubGap::Options options;
	options.inCharacters = true;
	options.tenths = 35;
	options.baud = 9600;
	VubGap framing(options);
	std::array<uint8, 8> out{};
	std::size_t length = 0;
	SIM_EXPECT(framing.Push(0x11, 0, out, length) == VubFrame::pending);
	SIM_EXPECT(framing.Poll(4010, out, length) == VubFrame::pending);
	SIM_EXPECT(framing.Poll(4011, out, length) == VubFrame::packet);
	SIM_EXPECT(length == 1);
}


// The baud rate changes at run time: at 19200 the same 3.5 character times are 38500000 / 19200 = 2005.21 us, rounded up to 2006.
void TestSetBaudChangesGap() {
	VubGap::Options options;
	options.inCharacters = true;
	options.tenths = 35;
	options.baud = 9600;
	VubGap framing(options);
	SIM_EXPECT(framing.SetBaud(19200) == ResultStatus::ok);
	std::array<uint8, 8> out{};
	std::size_t length = 0;
	SIM_EXPECT(framing.Push(0x22, 10000, out, length) == VubFrame::pending);
	SIM_EXPECT(framing.Poll(12005, out, length) == VubFrame::pending);
	SIM_EXPECT(framing.Poll(12006, out, length) == VubFrame::packet);
	SIM_EXPECT(length == 1);
	SIM_EXPECT(framing.SetBaud(0) == ResultStatus::invalidParameter);
}


// The send side waits for the gap after the last byte received: a byte at 0 gives Ready false until 1000, and true from 1000 on.
void TestReadyWaitsAfterReceive() {
	VubGap framing(GapOptions(1000));
	std::array<uint8, 8> out{};
	std::size_t length = 0;
	SIM_EXPECT(framing.Push(0x11, 0, out, length) == VubFrame::pending);
	SIM_EXPECT(!framing.Ready(999));
	SIM_EXPECT(framing.Ready(1000));
}


// The send side waits for the bytes it wrote: 10 bytes at 9600 baud are 110 bits, which take 11458.33 us. The end is rounded up to a
// whole microsecond, 11459, and the gap counts from it. Ready is false at 12458 and true at 12459.
void TestReadyWaitsAfterSend() {
	VubGap::Options options;
	options.gapUs = 1000;
	options.baud = 9600;
	options.timeoutUs = 50000;
	VubGap framing(options);
	framing.Sent(0, 10);
	SIM_EXPECT(!framing.Ready(12458));
	SIM_EXPECT(framing.Ready(12459));
}


// The end of a send is never estimated short: 100 bytes at 230400 baud are 1100 bits, which take 4774.3 us. The end is 4775, and the
// gap of 1750 counts from it: Ready is false at 6524 and true at 6525.
void TestSentEndAtBaud230400() {
	VubGap::Options options;
	options.gapUs = 1750;
	options.baud = 230400;
	options.timeoutUs = 50000;
	VubGap framing(options);
	framing.Sent(0, 100);
	SIM_EXPECT(!framing.Ready(6524));
	SIM_EXPECT(framing.Ready(6525));
}


// 256 bytes at 921600 baud are 2816 bits, which take 3055.56 us. The end is 3056, and the gap of 1750 ends at 4806: Ready is false at
// 4805 and true at 4806.
void TestSentEndAtBaud921600() {
	VubGap::Options options;
	options.gapUs = 1750;
	options.baud = 921600;
	options.timeoutUs = 50000;
	VubGap framing(options);
	framing.Sent(0, 256);
	SIM_EXPECT(!framing.Ready(4805));
	SIM_EXPECT(framing.Ready(4806));
}


// A line that never goes quiet does not hold a send for ever: after the timeout, Ready is true. Sent 1000 bytes at 9600 baud keeps the
// line busy for 1145000 us. The wait starts at 1000 and ends at 6000 (timeout 5000).
void TestReadyTimesOut() {
	VubGap::Options options;
	options.gapUs = 1000;
	options.baud = 9600;
	options.timeoutUs = 5000;
	VubGap framing(options);
	framing.Sent(0, 1000);
	SIM_EXPECT(!framing.Ready(1000));
	SIM_EXPECT(!framing.Ready(5999));
	SIM_EXPECT(framing.Ready(6000));
}


// Without a baud rate the send side counts the gap from the time of the Sent call: the bytes are assumed to leave at once.
void TestReadyWithoutBaud() {
	VubGap framing(GapOptions(1000));
	framing.Sent(500, 4);
	SIM_EXPECT(!framing.Ready(1499));
	SIM_EXPECT(framing.Ready(1500));
}


// A byte that arrives exactly the gap after the last one ends the packet before it: 11 at 0 and 22 at 1000, with a gap of 1000. The
// packet {11} comes out, and 22 starts the next one, which ends at 2000.
void TestByteAtExactGapEndsPacket() {
	VubGap framing(GapOptions(1000));
	std::array<uint8, 16> out{};
	std::size_t length = 0;
	SIM_EXPECT(framing.Push(0x11, 0, out, length) == VubFrame::pending);
	SIM_EXPECT(framing.Push(0x22, 1000, out, length) == VubFrame::packet);
	SIM_EXPECT(length == 1);
	SIM_EXPECT(out[0] == 0x11);
	SIM_EXPECT(framing.Poll(1999, out, length) == VubFrame::pending);
	SIM_EXPECT(framing.Poll(2000, out, length) == VubFrame::packet);
	SIM_EXPECT(length == 1);
	SIM_EXPECT(out[0] == 0x22);
}


// A single byte carried past the gap, then silence: 11 at 0 and 22 at 2000 give the packet {11}, and 22 is carried. With no byte after
// it, the silence of 1000 us after 22 ends its packet at 3000, which Poll gives.
void TestCarriedByteDeliveredBySilence() {
	VubGap framing(GapOptions(1000));
	std::array<uint8, 16> out{};
	std::size_t length = 0;
	SIM_EXPECT(framing.Push(0x11, 0, out, length) == VubFrame::pending);
	SIM_EXPECT(framing.Push(0x22, 2000, out, length) == VubFrame::packet);
	SIM_EXPECT(framing.Poll(2500, out, length) == VubFrame::pending);
	SIM_EXPECT(framing.Poll(2999, out, length) == VubFrame::pending);
	SIM_EXPECT(framing.Poll(3000, out, length) == VubFrame::packet);
	SIM_EXPECT(length == 1);
	SIM_EXPECT(out[0] == 0x22);
}


// Reset right after a carried byte drops the carried byte: 11 at 0 and 22 at 2000 leave 22 carried, and Reset clears it. The first
// packet after it is 33 at 5000, with no stale 22 in front.
void TestResetClearsCarriedByte() {
	VubGap framing(GapOptions(1000));
	std::array<uint8, 16> out{};
	std::size_t length = 0;
	SIM_EXPECT(framing.Push(0x11, 0, out, length) == VubFrame::pending);
	SIM_EXPECT(framing.Push(0x22, 2000, out, length) == VubFrame::packet);
	framing.Reset();
	SIM_EXPECT(framing.Push(0x33, 5000, out, length) == VubFrame::pending);
	SIM_EXPECT(framing.Poll(5999, out, length) == VubFrame::pending);
	SIM_EXPECT(framing.Poll(6000, out, length) == VubFrame::packet);
	SIM_EXPECT(length == 1);
	SIM_EXPECT(out[0] == 0x33);
}


// Gap, baud and timeout are options of the block: the defaults are 1750 us and 100000 us, and no baud rate.
void TestDefaults() {
	VubGap::Options options;
	SIM_EXPECT(options.gapUs == 1750);
	SIM_EXPECT(options.timeoutUs == 100000);
	SIM_EXPECT(options.baud == 0);
	SIM_EXPECT(!options.inCharacters);
}


// The framing contract over time: random packets with random silences between them, random polling and random loss. Without loss every
// packet comes out exactly, and the frames after a lossy part come out again.
void TestFramingContractNoLoss() {
	VubGap framing(GapOptions(1000));
	SIM_EXPECT(VubCheckTimedFraming(framing, 3, 200, 32, 0, 1000));
}


// With loss, a frame that loses a byte comes out short, and the gap cannot tell. The clean frames after the lossy part come out exactly.
void TestResyncContractWithLoss() {
	VubGap framing(GapOptions(1000));
	SIM_EXPECT(VubCheckTimedFraming(framing, 9, 200, 32, 15, 1000));
}


// The concept: the gap framing satisfies VubFraming and VubTimedFraming. A framing that uses time but has no Poll, Ready or Sent does not
// satisfy VubFraming, so the builder refuses it with the concept's message.
struct UntimedTimedFraming {
	static constexpr bool usesTime = true;


	static constexpr std::size_t Growth(std::size_t length) {
		return length;
	}


	Result<std::size_t> Frame(const VubMeta&, std::span<const uint8> in, std::span<uint8> out) {
		(void)in;
		(void)out;
		return std::size_t{0};
	}


	VubFrame Push(uint8 byte, uint32 nowUs, std::span<uint8> out, std::size_t& length) {
		(void)byte;
		(void)nowUs;
		(void)out;
		(void)length;
		return VubFrame::pending;
	}
};


static_assert(VubFraming<VubGap>);
static_assert(VubTimedFraming<VubGap>);
static_assert(!VubFraming<UntimedTimedFraming>);


int main() {
	TestFrameIsThePayload();
	TestFrameEndsAtSilence();
	TestByteAfterGapStartsNextFrame();
	TestGapEndsOnlyAtSilence();
	TestResetDropsPartialFrame();
	TestTooLong();
	TestCharacterGapAtBaud();
	TestSetBaudChangesGap();
	TestReadyWaitsAfterReceive();
	TestReadyWaitsAfterSend();
	TestReadyTimesOut();
	TestReadyWithoutBaud();
	TestSentEndAtBaud230400();
	TestSentEndAtBaud921600();
	TestByteAtExactGapEndsPacket();
	TestCarriedByteDeliveredBySilence();
	TestResetClearsCarriedByte();
	TestDefaults();
	TestFramingContractNoLoss();
	TestResyncContractWithLoss();
	return SimFinish("TestVubGap");
}
