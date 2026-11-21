#include <VHAL.h>
#include "NcmTestDevices.h"
#include "SimAssert.h"
#include "SimHost.h"
#include <array>
#include <cstddef>
#include <span>


// The host's NTB input size (SET_NTB_INPUT_SIZE) against the blocks the device sends. The host may lower the size below the largest NTB
// the device advertises, at any time; a block the device sends must fit the size the host holds when the block goes out. The
// WideFunction fixture advertises 4096 bytes, so a lowered 2048-byte size is a real cut. The byte layouts are written by hand from NCM 1.0
// section 3 with the frame sizes of the test: a 1514-byte frame takes 1544 bytes in a block of its own (NTH, frame, NDP with one pair).
namespace {


// A 1514-byte frame whose byte at index i is i (the low byte), so the block's frame bytes are easy to check.
std::array<uint8, 1514> MakeBigFrame() {
	std::array<uint8, 1514> frame = {};
	for (std::size_t index = 0; index < frame.size(); index++) {
		frame[index] = static_cast<uint8>(index);
	}

	return frame;
}


// The 4-byte SET_NTB_INPUT_SIZE request: dwNtbInMaxSize, little-endian.
ResultStatus SetInputSize(WideNcmFixture& fixture, uint32 size) {
	const uint8 data[4] = {
		static_cast<uint8>(size & 0xFF),
		static_cast<uint8>((size >> 8) & 0xFF),
		static_cast<uint8>((size >> 16) & 0xFF),
		static_cast<uint8>((size >> 24) & 0xFF)
	};
	uint32 received = 0;
	return SimHost::Control(fixture.sim, SimHost::MakeSetup(0x21, 0x86, 0x0000, 0x0000, 4), std::span<const uint8>(data), std::span<uint8>(), received);
}


// Takes the armed IN transfer of the bulk IN endpoint and returns its length, or 0 when nothing is armed. The bytes are copied to in,
// which must hold the largest block the test expects.
template<typename TFixture>
uint32 TakeIn(TFixture& fixture, std::span<uint8> in) {
	if (!fixture.sim.HasArmedTransfer(0x81)) {
		return 0;
	}

	uint32 received = 0;
	SIM_EXPECT_EQ(fixture.sim.HostIn(0x81, in, received), ResultStatus::ok);
	return received;
}


// The 8-byte SET_NTB_INPUT_SIZE request with a datagram limit: the size, then wNtbInMaxDatagrams, then the reserved word.
ResultStatus SetInputSizeAndDatagrams(NcmFixture& fixture, uint32 size, uint16 datagrams) {
	const uint8 data[8] = {
		static_cast<uint8>(size & 0xFF),
		static_cast<uint8>((size >> 8) & 0xFF),
		static_cast<uint8>((size >> 16) & 0xFF),
		static_cast<uint8>((size >> 24) & 0xFF),
		static_cast<uint8>(datagrams & 0xFF),
		static_cast<uint8>((datagrams >> 8) & 0xFF),
		0x00,
		0x00
	};
	uint32 received = 0;
	return SimHost::Control(fixture.sim, SimHost::MakeSetup(0x21, 0x86, 0x0000, 0x0000, 8), std::span<const uint8>(data), std::span<uint8>(), received);
}


// Opens the data path of a wide device with a 4096-byte input size.
void OpenWideSession(WideNcmFixture& fixture) {
	fixture.Configure();
	SIM_EXPECT_EQ(fixture.SelectAlternate(1), ResultStatus::ok);
}


// Plain path: the host lowers its input size before the frames are packed. Each block holds one 1514-byte frame, so the two frames take two
// 1544-byte blocks, and the sequence numbers are 0 and 1.
void TestInputSizeLoweredBeforePacking() {
	WideNcmFixture fixture;
	OpenWideSession(fixture);
	SIM_EXPECT_EQ(SetInputSize(fixture, 2048), ResultStatus::ok);

	std::array<uint8, 1514> big = MakeBigFrame();
	SIM_EXPECT_EQ(fixture.function.SendFrame(big), ResultStatus::ok);
	SIM_EXPECT_EQ(fixture.function.SendFrame(big), ResultStatus::ok);
	fixture.device.Process();

	std::array<uint8, 4096> in = {};
	SIM_EXPECT_EQ(TakeIn(fixture, in), 1544);
	SIM_EXPECT_EQ(in[6], 0x00);

	// The second frame is packed on the next call, once the first block has left the endpoint.
	fixture.device.Process();
	SIM_EXPECT_EQ(TakeIn(fixture, in), 1544);
	SIM_EXPECT_EQ(in[6], 0x01);
	SIM_EXPECT_EQ(fixture.function.GetCounters().framesSent, 2);
	SIM_EXPECT_EQ(fixture.function.GetCounters().ntbsSent, 2);
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


// Halt path: a block of two frames (3064 bytes) is in flight when the bulk IN endpoint halts, so the transfer is abandoned and the block
// waits. The host lowers its input size to 2048 while the endpoint is halted, and the halt clears. The 3064-byte block does not fit that
// size and must not be sent: it is discarded, and its two frames are counted as dropped, not as sent. The next frame goes out as a block
// of the lowered size, from sequence 0 (the discarded block never existed for the host).
void TestInputShrinkWhileHaltedDiscardsBlock() {
	WideNcmFixture fixture;
	OpenWideSession(fixture);

	std::array<uint8, 1514> big = MakeBigFrame();
	SIM_EXPECT_EQ(fixture.function.SendFrame(big), ResultStatus::ok);
	SIM_EXPECT_EQ(fixture.function.SendFrame(big), ResultStatus::ok);
	fixture.device.Process();
	SIM_EXPECT(fixture.sim.HasArmedTransfer(0x81));

	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x02, 3, 0x0000, 0x0081, 0)), ResultStatus::ok);
	SIM_EXPECT(!fixture.sim.HasArmedTransfer(0x81));
	SIM_EXPECT_EQ(SetInputSize(fixture, 2048), ResultStatus::ok);
	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x02, 1, 0x0000, 0x0081, 0)), ResultStatus::ok);
	fixture.device.Process();
	SIM_EXPECT(!fixture.sim.HasArmedTransfer(0x81));

	NcmTest::WideFunction::Counters counters = fixture.function.GetCounters();
	SIM_EXPECT_EQ(counters.framesSent, 0);
	SIM_EXPECT_EQ(counters.ntbsSent, 0);
	SIM_EXPECT_EQ(counters.framesDroppedSend, 2);

	SIM_EXPECT_EQ(fixture.function.SendFrame(big), ResultStatus::ok);
	fixture.device.Process();
	std::array<uint8, 4096> in = {};
	SIM_EXPECT_EQ(TakeIn(fixture, in), 1544);
	SIM_EXPECT_EQ(in[6], 0x00);
	SIM_EXPECT_EQ(in[7], 0x00);
	SIM_EXPECT_EQ(fixture.function.GetCounters().framesSent, 1);
	SIM_EXPECT_EQ(fixture.function.GetCounters().ntbsSent, 1);
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


// The same block when the host's size is still 4096 at the clear: the block fits, so it goes out unchanged, and it is counted once.
void TestHaltedBlockResentWhenItFits() {
	WideNcmFixture fixture;
	OpenWideSession(fixture);

	std::array<uint8, 1514> big = MakeBigFrame();
	SIM_EXPECT_EQ(fixture.function.SendFrame(big), ResultStatus::ok);
	SIM_EXPECT_EQ(fixture.function.SendFrame(big), ResultStatus::ok);
	fixture.device.Process();

	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x02, 3, 0x0000, 0x0081, 0)), ResultStatus::ok);
	fixture.device.Process();
	SIM_EXPECT(!fixture.sim.HasArmedTransfer(0x81));
	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x02, 1, 0x0000, 0x0081, 0)), ResultStatus::ok);
	SIM_EXPECT(fixture.sim.HasArmedTransfer(0x81));

	std::array<uint8, 4096> in = {};
	// 3064 = 0x0BF8: the block length, then the NDP at 3044 = 0x0BE4.
	SIM_EXPECT_EQ(TakeIn(fixture, in), 3064);
	SIM_EXPECT_EQ(in[8], 0xF8);
	SIM_EXPECT_EQ(in[9], 0x0B);
	SIM_EXPECT_EQ(in[10], 0xE4);
	SIM_EXPECT_EQ(in[11], 0x0B);
	SIM_EXPECT_EQ(fixture.function.GetCounters().framesSent, 2);
	SIM_EXPECT_EQ(fixture.function.GetCounters().ntbsSent, 1);
	SIM_EXPECT_EQ(fixture.function.GetCounters().framesDroppedSend, 0);
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


// A block of two 14-byte frames (64 bytes) waits behind a halted IN endpoint, and the session ends (alternate 0) before the halt clears. The
// block is withdrawn with the session: its two frames are counted as dropped and the sent counts go back. The next session starts at sequence 0.
void TestWaitingBlockWithdrawnAtSessionEnd() {
	NcmFixture fixture;
	fixture.Configure();
	SIM_EXPECT_EQ(fixture.SelectAlternate(1), ResultStatus::ok);

	const uint8 frameA[14] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x02, 0x00, 0x00, 0x00, 0x00, 0x01, 0x08, 0x00};
	SIM_EXPECT_EQ(fixture.function.SendFrame(frameA), ResultStatus::ok);
	SIM_EXPECT_EQ(fixture.function.SendFrame(frameA), ResultStatus::ok);
	fixture.device.Process();
	SIM_EXPECT(fixture.sim.HasArmedTransfer(0x81));

	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x02, 3, 0x0000, 0x0081, 0)), ResultStatus::ok);
	SIM_EXPECT_EQ(fixture.SelectAlternate(0), ResultStatus::ok);
	fixture.device.Process();
	SIM_EXPECT(!fixture.sim.HasArmedTransfer(0x81));
	NcmTest::Function::Counters counters = fixture.function.GetCounters();
	SIM_EXPECT_EQ(counters.framesSent, 0);
	SIM_EXPECT_EQ(counters.ntbsSent, 0);
	SIM_EXPECT_EQ(counters.framesDroppedSend, 2);

	SIM_EXPECT_EQ(fixture.SelectAlternate(1), ResultStatus::ok);
	SIM_EXPECT_EQ(fixture.function.SendFrame(frameA), ResultStatus::ok);
	fixture.device.Process();
	std::array<uint8, 2048> in = {};
	SIM_EXPECT_EQ(TakeIn(fixture, in), 44);
	SIM_EXPECT_EQ(in[6], 0x00);
	SIM_EXPECT_EQ(fixture.function.GetCounters().framesSent, 1);
	SIM_EXPECT_EQ(fixture.function.GetCounters().ntbsSent, 1);
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


// A datagram limit the host sets with the 8-byte form: one datagram per NTB. Two small frames take two blocks, and the second has sequence 1.
void TestDatagramLimitSplitsBlocks() {
	NcmFixture fixture;
	fixture.Configure();
	SIM_EXPECT_EQ(fixture.SelectAlternate(1), ResultStatus::ok);
	SIM_EXPECT_EQ(SetInputSizeAndDatagrams(fixture, 2048, 1), ResultStatus::ok);

	// A 14-byte frame takes 44 bytes in a block of its own: NTH, the frame, the NDP at 28 with one pair and the terminator.
	const uint8 frameA[14] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x02, 0x00, 0x00, 0x00, 0x00, 0x01, 0x08, 0x00};
	SIM_EXPECT_EQ(fixture.function.SendFrame(frameA), ResultStatus::ok);
	SIM_EXPECT_EQ(fixture.function.SendFrame(frameA), ResultStatus::ok);
	fixture.device.Process();

	std::array<uint8, 2048> in = {};
	SIM_EXPECT_EQ(TakeIn(fixture, in), 44);
	SIM_EXPECT_EQ(in[6], 0x00);
	fixture.device.Process();
	SIM_EXPECT_EQ(TakeIn(fixture, in), 44);
	SIM_EXPECT_EQ(in[6], 0x01);
	SIM_EXPECT_EQ(fixture.function.GetCounters().framesSent, 2);
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


}


int main() {
	TestInputSizeLoweredBeforePacking();
	TestInputShrinkWhileHaltedDiscardsBlock();
	TestHaltedBlockResentWhenItFits();
	TestWaitingBlockWithdrawnAtSessionEnd();
	TestDatagramLimitSplitsBlocks();
	return SimTest::Finish("TestNcmInput");
}
