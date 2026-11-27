#include <VHAL.h>
#include "NcmTestDevices.h"
#include "SimAssert.h"
#include "SimHost.h"
#include <Drivers/Interface/USB/Class/CDC/NcmNtb16.h>
#include <array>
#include <cstddef>
#include <span>


// The data path end to end through the simulated controller: Ethernet frames are sent and received inside NTB-16 blocks over the bulk
// pair of NcmTest::ExplicitUsb. The NTBs are written by hand from NCM 1.0 section 3 (the NTH16 at 0, the datagrams at the offsets the
// divisor allows, the NDP16 with "NCM0", its entries and the (0, 0) terminator), and every expected byte is written here, not taken
// from the function. The frames are Ethernet frames of the stated lengths: 14 bytes (a header), 20 bytes and 36 bytes.
namespace {


// Frame A: a 14-byte header only, to ff:ff:ff:ff:ff:ff from 02:00:00:00:00:01, type IPv4 (08 00).
constexpr uint8 FrameA[14] = {
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0x02, 0x00, 0x00, 0x00, 0x00, 0x01,
	0x08, 0x00
};


// Frame B: 20 bytes, a header and six payload bytes 10 to 15.
constexpr uint8 FrameB[20] = {
	0x02, 0x00, 0x00, 0x00, 0x00, 0x02,
	0x02, 0x00, 0x00, 0x00, 0x00, 0x01,
	0x08, 0x00,
	0x10, 0x11, 0x12, 0x13, 0x14, 0x15
};


// Frame C: 36 bytes, a header and 22 payload bytes 20 to 35.
constexpr uint8 FrameC[36] = {
	0x02, 0x00, 0x00, 0x00, 0x00, 0x03,
	0x02, 0x00, 0x00, 0x00, 0x00, 0x01,
	0x08, 0x00,
	0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A,
	0x2B, 0x2C, 0x2D, 0x2E, 0x2F, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35
};


// Two frames, A at 12 and B at 28 (the pad at 26 and 27), and the NDP at 48 listing (12, 14), (28, 20) and the terminator. Sequence 0.
// Block length 68 (0x44), wNdpIndex 48 (0x30), NDP wLength 20 (0x14).
constexpr uint8 TwoFrameBlock[68] = {
	0x4E, 0x43, 0x4D, 0x48, 0x0C, 0x00, 0x00, 0x00, 0x44, 0x00, 0x30, 0x00,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x02, 0x00, 0x00, 0x00, 0x00, 0x01, 0x08, 0x00,
	0x00, 0x00,
	0x02, 0x00, 0x00, 0x00, 0x00, 0x02, 0x02, 0x00, 0x00, 0x00, 0x00, 0x01, 0x08, 0x00, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15,
	0x4E, 0x43, 0x4D, 0x30, 0x14, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x0E, 0x00, 0x1C, 0x00, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00
};


static_assert(sizeof(TwoFrameBlock) == 68, "the two-frame block is 68 bytes");


// Frame A alone, sequence 0: block length 44 (0x2C), NDP at 28 (0x1C) with (12, 14). The NDP is 16 bytes, as its wLength says.
constexpr uint8 FrameABlock[44] = {
	0x4E, 0x43, 0x4D, 0x48, 0x0C, 0x00, 0x00, 0x00, 0x2C, 0x00, 0x1C, 0x00,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x02, 0x00, 0x00, 0x00, 0x00, 0x01, 0x08, 0x00,
	0x00, 0x00,
	0x4E, 0x43, 0x4D, 0x30, 0x10, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x0E, 0x00, 0x00, 0x00, 0x00, 0x00
};


static_assert(sizeof(FrameABlock) == 44, "the single-frame block is 44 bytes");


// Frame C alone, sequence 0: 12 + 36 = 48 bytes of NTH and frame, then the NDP at 48 (0x30) with (12, 36 = 0x24). Block length 64, a
// whole packet, so the device sends a zero-length packet after it.
constexpr uint8 FrameCBlock[64] = {
	0x4E, 0x43, 0x4D, 0x48, 0x0C, 0x00, 0x00, 0x00, 0x40, 0x00, 0x30, 0x00,
	0x02, 0x00, 0x00, 0x00, 0x00, 0x03, 0x02, 0x00, 0x00, 0x00, 0x00, 0x01, 0x08, 0x00,
	0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A,
	0x2B, 0x2C, 0x2D, 0x2E, 0x2F, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35,
	0x4E, 0x43, 0x4D, 0x30, 0x10, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x24, 0x00, 0x00, 0x00, 0x00, 0x00
};


static_assert(sizeof(FrameCBlock) == 64, "the block of frame C is 64 bytes");


// Frame B alone, sequence 1: 12 + 20 = 32, the NDP at 32 (0x20) with (12, 20 = 0x14). Block length 48 (0x30).
constexpr uint8 FrameBBlockSeq1[48] = {
	0x4E, 0x43, 0x4D, 0x48, 0x0C, 0x00, 0x01, 0x00, 0x30, 0x00, 0x20, 0x00,
	0x02, 0x00, 0x00, 0x00, 0x00, 0x02, 0x02, 0x00, 0x00, 0x00, 0x00, 0x01, 0x08, 0x00, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15,
	0x4E, 0x43, 0x4D, 0x30, 0x10, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00
};


static_assert(sizeof(FrameBBlockSeq1) == 48, "the block of frame B is 48 bytes");


// Counts the frames the application is handed, through the frame-received delegate.
struct FrameEvents {
	uint32 blocks = 0;
};


void CountFrameBlock(void* context) {
	static_cast<FrameEvents*>(context)->blocks++;
}


// Takes the armed IN transfer of the bulk IN endpoint and checks it against the expected block, byte for byte.
template<std::size_t Size>
void ExpectInBlock(NcmFixture& fixture, const uint8 (&expected)[Size]) {
	SIM_EXPECT(fixture.sim.HasArmedTransfer(0x81));
	std::array<uint8, 2048> in = {};
	uint32 received = 0;
	SIM_EXPECT_EQ(fixture.sim.HostIn(0x81, in, received), ResultStatus::ok);
	SIM_EXPECT_EQ(received, Size);
	if (received == Size) {
		for (std::size_t index = 0; index < Size; index++) {
			SIM_EXPECT_EQ(in[index], expected[index]);
		}
	}
}


// The host sends one block on the bulk OUT endpoint, which completes with its short or zero-length packet, then the device parses it.
template<std::size_t Size>
void HostSendBlock(NcmFixture& fixture, const uint8 (&block)[Size]) {
	SIM_EXPECT(fixture.sim.HasArmedTransfer(0x02));
	SIM_EXPECT(fixture.sim.HostOut(0x02, block).IsOk());
	fixture.device.Process();
}


// A receive path with the function's frame events counted and the data path open.
void OpenSession(NcmFixture& fixture, FrameEvents& events) {
	SIM_EXPECT_EQ(fixture.function.SetFrameReceivedHandler(NcmTest::Function::FrameReceivedDelegate::FromRaw(&CountFrameBlock, &events)), ResultStatus::ok);
	fixture.Configure();
	SIM_EXPECT_EQ(fixture.SelectAlternate(1), ResultStatus::ok);
}


// Takes the next received frame and checks its bytes against the expected frame.
template<std::size_t Size>
void ExpectReceivedFrame(NcmFixture& fixture, const uint8 (&expected)[Size]) {
	SIM_EXPECT_EQ(fixture.function.NextFrameLength(), Size);
	std::array<uint8, 1514> frame = {};
	SIM_EXPECT_EQ(fixture.function.ReceiveFrame(frame), Size);
	for (std::size_t index = 0; index < Size; index++) {
		SIM_EXPECT_EQ(frame[index], expected[index]);
	}
}


// Two frames in one block: both are received, in the order the block lists them, and the block counts as one delivery.
void TestBlockWithTwoFramesIn() {
	NcmFixture fixture;
	FrameEvents events;
	OpenSession(fixture, events);

	HostSendBlock(fixture, TwoFrameBlock);
	SIM_EXPECT(fixture.sim.HasArmedTransfer(0x02));
	SIM_EXPECT_EQ(events.blocks, 1);
	ExpectReceivedFrame(fixture, FrameA);
	ExpectReceivedFrame(fixture, FrameB);
	SIM_EXPECT_EQ(fixture.function.NextFrameLength(), 0);

	NcmTest::Function::Counters counters = fixture.function.GetCounters();
	SIM_EXPECT_EQ(counters.ntbsReceived, 1);
	SIM_EXPECT_EQ(counters.framesReceived, 2);
	SIM_EXPECT_EQ(counters.ntbsMalformed, 0);
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


// Two frames queued become one block of the hand-written 68 bytes, with sequence 0.
void TestBlockWithTwoFramesOut() {
	NcmFixture fixture;
	FrameEvents events;
	OpenSession(fixture, events);

	SIM_EXPECT_EQ(fixture.function.SendFrame(FrameA), ResultStatus::ok);
	SIM_EXPECT_EQ(fixture.function.SendFrame(FrameB), ResultStatus::ok);
	fixture.device.Process();
	ExpectInBlock(fixture, TwoFrameBlock);

	NcmTest::Function::Counters counters = fixture.function.GetCounters();
	SIM_EXPECT_EQ(counters.framesSent, 2);
	SIM_EXPECT_EQ(counters.ntbsSent, 1);
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


// One frame makes a 44-byte block, which is not a whole packet: no zero-length packet follows it. The toggle of the bulk IN endpoint
// moves once, for its one packet.
void TestShortBlockHasNoZeroLengthPacket() {
	NcmFixture fixture;
	FrameEvents events;
	OpenSession(fixture, events);

	SIM_EXPECT_EQ(fixture.function.SendFrame(FrameA), ResultStatus::ok);
	fixture.device.Process();
	bool before = fixture.sim.GetDataToggle(0x81);
	ExpectInBlock(fixture, FrameABlock);
	SIM_EXPECT(fixture.sim.GetDataToggle(0x81) != before);
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


// A 64-byte block is a whole packet, and smaller than the host's 2048-byte input size, so a zero-length packet follows it: two packets
// move the toggle twice and leave it where it was.
void TestWholePacketBlockHasZeroLengthPacket() {
	NcmFixture fixture;
	FrameEvents events;
	OpenSession(fixture, events);

	SIM_EXPECT_EQ(fixture.function.SendFrame(FrameC), ResultStatus::ok);
	fixture.device.Process();
	bool before = fixture.sim.GetDataToggle(0x81);
	ExpectInBlock(fixture, FrameCBlock);
	SIM_EXPECT(fixture.sim.GetDataToggle(0x81) == before);
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


// Frames queued while a block is in flight wait for the next one: the block in flight is not replaced.
void TestQueueWaitsForBlockInFlight() {
	NcmFixture fixture;
	FrameEvents events;
	OpenSession(fixture, events);

	SIM_EXPECT_EQ(fixture.function.SendFrame(FrameA), ResultStatus::ok);
	fixture.device.Process();
	SIM_EXPECT_EQ(fixture.function.SendFrame(FrameB), ResultStatus::ok);
	fixture.device.Process();
	ExpectInBlock(fixture, FrameABlock);

	fixture.device.Process();
	ExpectInBlock(fixture, FrameBBlockSeq1);
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


// The sequence number counts the blocks of a session from 0, and a new session starts it at 0 again.
void TestSequenceRestartsWithSession() {
	NcmFixture fixture;
	FrameEvents events;
	OpenSession(fixture, events);

	SIM_EXPECT_EQ(fixture.function.SendFrame(FrameA), ResultStatus::ok);
	fixture.device.Process();
	ExpectInBlock(fixture, FrameABlock);

	SIM_EXPECT_EQ(fixture.function.SendFrame(FrameB), ResultStatus::ok);
	fixture.device.Process();
	ExpectInBlock(fixture, FrameBBlockSeq1);

	SIM_EXPECT_EQ(fixture.SelectAlternate(0), ResultStatus::ok);
	SIM_EXPECT_EQ(fixture.SelectAlternate(1), ResultStatus::ok);
	SIM_EXPECT_EQ(fixture.function.SendFrame(FrameA), ResultStatus::ok);
	fixture.device.Process();
	ExpectInBlock(fixture, FrameABlock);
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


// A block whose frames would overflow the host's 2048-byte input size is cut: a 1514-byte frame takes 1544 bytes with its NTH and NDP, and
// the second such frame would need 3044 + 20 bytes, so each block holds one frame. The four frames take four blocks of 1544 bytes.
void TestInputSizeLimitsBlocks() {
	NcmFixture fixture;
	FrameEvents events;
	OpenSession(fixture, events);

	std::array<uint8, 1514> big = {};
	for (std::size_t index = 0; index < big.size(); index++) {
		big[index] = static_cast<uint8>(index);
	}

	for (uint32 index = 0; index < 4; index++) {
		SIM_EXPECT_EQ(fixture.function.SendFrame(big), ResultStatus::ok);
	}

	for (uint32 round = 0; round < 4; round++) {
		fixture.device.Process();
		SIM_EXPECT(fixture.sim.HasArmedTransfer(0x81));
		std::array<uint8, 2048> in = {};
		uint32 received = 0;
		SIM_EXPECT_EQ(fixture.sim.HostIn(0x81, in, received), ResultStatus::ok);
		// 1544 = 0x0608: the block length, then the NDP at 1528 (0x05F8) with the frame at 12 and the terminator. The NDP's wLength is
		// 16 (8 + two pairs of 4), its one entry is (12, 1514 = 0x05EA) at 1536, and the terminator (0, 0) follows at 1540.
		SIM_EXPECT_EQ(received, 1544);
		if (received == 1544) {
			SIM_EXPECT_EQ(in[8], 0x08);
			SIM_EXPECT_EQ(in[9], 0x06);
			SIM_EXPECT_EQ(in[10], 0xF8);
			SIM_EXPECT_EQ(in[11], 0x05);
			SIM_EXPECT_EQ(in[12], 0x00);
			SIM_EXPECT_EQ(in[1528], 0x4E);
			SIM_EXPECT_EQ(in[1532], 0x10);
			SIM_EXPECT_EQ(in[1533], 0x00);
			SIM_EXPECT_EQ(in[1536], 0x0C);
			SIM_EXPECT_EQ(in[1537], 0x00);
			SIM_EXPECT_EQ(in[1538], 0xEA);
			SIM_EXPECT_EQ(in[1539], 0x05);
			SIM_EXPECT_EQ(in[1540], 0x00);
		}
	}

	fixture.device.Process();
	SIM_EXPECT(!fixture.sim.HasArmedTransfer(0x81));
	SIM_EXPECT_EQ(fixture.function.GetCounters().framesSent, 4);
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


// The host's block with a broken rule delivers nothing and is counted by its rule. The next good block still delivers its frames.
void TestMalformedBlocksAreDropped() {
	NcmFixture fixture;
	FrameEvents events;
	OpenSession(fixture, events);

	// The signature is "NCMX".
	uint8 badSignature[68] = {};
	for (std::size_t index = 0; index < sizeof(badSignature); index++) {
		badSignature[index] = TwoFrameBlock[index];
	}

	badSignature[3] = 0x58;
	HostSendBlock(fixture, badSignature);
	SIM_EXPECT_EQ(fixture.function.GetMalformedCount(USBNcmNtb16Status::badSignature), 1);

	// The block length is 80, more than the 68 bytes the host sent.
	uint8 longBlock[68] = {};
	for (std::size_t index = 0; index < sizeof(longBlock); index++) {
		longBlock[index] = TwoFrameBlock[index];
	}

	longBlock[8] = 0x50;
	HostSendBlock(fixture, longBlock);
	SIM_EXPECT_EQ(fixture.function.GetMalformedCount(USBNcmNtb16Status::blockTooLarge), 1);

	// wNdpIndex 49 is not a multiple of 4.
	uint8 oddNdp[68] = {};
	for (std::size_t index = 0; index < sizeof(oddNdp); index++) {
		oddNdp[index] = TwoFrameBlock[index];
	}

	oddNdp[10] = 0x31;
	HostSendBlock(fixture, oddNdp);
	SIM_EXPECT_EQ(fixture.function.GetMalformedCount(USBNcmNtb16Status::ndpIndexInvalid), 1);

	// The NDP is "NCM1": each datagram would carry a CRC, which the function does not take.
	uint8 crcNdp[68] = {};
	for (std::size_t index = 0; index < sizeof(crcNdp); index++) {
		crcNdp[index] = TwoFrameBlock[index];
	}

	crcNdp[51] = 0x31;
	HostSendBlock(fixture, crcNdp);
	SIM_EXPECT_EQ(fixture.function.GetMalformedCount(USBNcmNtb16Status::ndpCrcUnsupported), 1);

	// The second datagram starts at 64 and would end at 84, past the block.
	uint8 outside[68] = {};
	for (std::size_t index = 0; index < sizeof(outside); index++) {
		outside[index] = TwoFrameBlock[index];
	}

	outside[60] = 0x40;
	HostSendBlock(fixture, outside);
	SIM_EXPECT_EQ(fixture.function.GetMalformedCount(USBNcmNtb16Status::datagramOutsideBlock), 1);

	// Nothing of the five bad blocks was delivered, and the endpoint is armed again.
	SIM_EXPECT_EQ(fixture.function.NextFrameLength(), 0);
	SIM_EXPECT_EQ(events.blocks, 0);
	SIM_EXPECT(fixture.sim.HasArmedTransfer(0x02));

	HostSendBlock(fixture, TwoFrameBlock);
	ExpectReceivedFrame(fixture, FrameA);
	ExpectReceivedFrame(fixture, FrameB);
	NcmTest::Function::Counters counters = fixture.function.GetCounters();
	SIM_EXPECT_EQ(counters.ntbsReceived, 6);
	SIM_EXPECT_EQ(counters.ntbsMalformed, 5);
	SIM_EXPECT_EQ(counters.framesReceived, 2);
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


// Deconfiguration with a block in flight: the block is gone with the endpoint, the frame queued behind it (FrameC) is dropped too, and the
// next session sends only its own frame, from sequence 0. The block of FrameB alone shows that FrameC did not survive.
void TestDeconfigureWithBlockInFlight() {
	NcmFixture fixture;
	FrameEvents events;
	OpenSession(fixture, events);

	SIM_EXPECT_EQ(fixture.function.SendFrame(FrameA), ResultStatus::ok);
	fixture.device.Process();
	SIM_EXPECT(fixture.sim.HasArmedTransfer(0x81));
	SIM_EXPECT_EQ(fixture.function.SendFrame(FrameC), ResultStatus::ok);
	SIM_EXPECT_EQ(fixture.Deconfigure(), ResultStatus::ok);
	SIM_EXPECT(!fixture.sim.HasArmedTransfer(0x81));

	fixture.Enumerate();
	SIM_EXPECT_EQ(fixture.SelectAlternate(1), ResultStatus::ok);
	fixture.device.Process();
	SIM_EXPECT(!fixture.sim.HasArmedTransfer(0x81));

	SIM_EXPECT_EQ(fixture.function.SendFrame(FrameB), ResultStatus::ok);
	fixture.device.Process();
	std::array<uint8, 2048> in = {};
	uint32 received = 0;
	SIM_EXPECT_EQ(fixture.sim.HostIn(0x81, in, received), ResultStatus::ok);
	// The block of frame B alone, sequence 0 (the bytes 6 and 7 are 00 00): 48 bytes, NDP at 32.
	SIM_EXPECT_EQ(received, 48);
	SIM_EXPECT_EQ(in[6], 0x00);
	SIM_EXPECT_EQ(in[7], 0x00);
	SIM_EXPECT_EQ(in[8], 0x30);
	SIM_EXPECT_EQ(in[10], 0x20);
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


// A block whose NDPs form a chain: the first NDP lists frame A and points at the second, which lists frame B. The block is 80 bytes: NTH at
// 0, frame A at 12, the NDP at 28 (wNextNdpIndex 64), frame B at 44, the NDP at 64 with the terminator at its end. Both frames are delivered
// in the order the chain lists them, as one block.
void TestChainedNdpsDeliverAllFrames() {
	constexpr uint8 ChainedBlock[80] = {
		0x4E, 0x43, 0x4D, 0x48, 0x0C, 0x00, 0x00, 0x00, 0x50, 0x00, 0x1C, 0x00,
		0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x02, 0x00, 0x00, 0x00, 0x00, 0x01, 0x08, 0x00,
		0x00, 0x00,
		0x4E, 0x43, 0x4D, 0x30, 0x10, 0x00, 0x40, 0x00, 0x0C, 0x00, 0x0E, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x02, 0x00, 0x00, 0x00, 0x00, 0x02, 0x02, 0x00, 0x00, 0x00, 0x00, 0x01, 0x08, 0x00, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15,
		0x4E, 0x43, 0x4D, 0x30, 0x10, 0x00, 0x00, 0x00, 0x2C, 0x00, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00
	};


	static_assert(sizeof(ChainedBlock) == 80, "the chained block is 80 bytes");

	NcmFixture fixture;
	FrameEvents events;
	OpenSession(fixture, events);

	HostSendBlock(fixture, ChainedBlock);
	SIM_EXPECT_EQ(events.blocks, 1);
	ExpectReceivedFrame(fixture, FrameA);
	ExpectReceivedFrame(fixture, FrameB);
	SIM_EXPECT_EQ(fixture.function.NextFrameLength(), 0);

	NcmTest::Function::Counters counters = fixture.function.GetCounters();
	SIM_EXPECT_EQ(counters.ntbsReceived, 1);
	SIM_EXPECT_EQ(counters.framesReceived, 2);
	SIM_EXPECT_EQ(counters.ntbsMalformed, 0);
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


// A zero-length packet on the bulk OUT endpoint completes the armed transfer with no data. The function re-arms it, and the next block is
// received as usual; no block is counted for the empty transfer.
void TestZeroLengthOutTransferRearms() {
	NcmFixture fixture;
	FrameEvents events;
	OpenSession(fixture, events);

	SIM_EXPECT(fixture.sim.HasArmedTransfer(0x02));
	SIM_EXPECT(fixture.sim.HostOut(0x02, std::span<const uint8>()).IsOk());
	fixture.device.Process();
	SIM_EXPECT(fixture.sim.HasArmedTransfer(0x02));
	SIM_EXPECT_EQ(events.blocks, 0);
	SIM_EXPECT_EQ(fixture.function.GetCounters().ntbsReceived, 0);

	HostSendBlock(fixture, FrameABlock);
	ExpectReceivedFrame(fixture, FrameA);
	SIM_EXPECT_EQ(fixture.function.GetCounters().ntbsReceived, 1);
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


// A block with five frames when the receive queue holds four: the first four are delivered and the fifth is dropped and counted. The block
// is 124 bytes: five 14-byte frames at 12, 28, 44, 60 and 76, and the NDP at 92 with six pairs (32 bytes).
void TestReceiveQueueFullDropsFrames() {
	NcmFixture fixture;
	FrameEvents events;
	OpenSession(fixture, events);

	std::array<uint8, 124> block = {};
	block[0] = 0x4E;
	block[1] = 0x43;
	block[2] = 0x4D;
	block[3] = 0x48;
	block[4] = 0x0C;
	block[8] = 0x7C;
	block[10] = 0x5C;
	for (std::size_t frame = 0; frame < 5; frame++) {
		for (std::size_t index = 0; index < sizeof(FrameA); index++) {
			block[12 + 16 * frame + index] = FrameA[index];
		}
	}

	const std::size_t ndp = 92;
	block[ndp] = 0x4E;
	block[ndp + 1] = 0x43;
	block[ndp + 2] = 0x4D;
	block[ndp + 3] = 0x30;
	block[ndp + 4] = 0x20;
	// Each pair is wDatagramIndex (2 bytes, little-endian) then wDatagramLength (2 bytes); the sixth pair stays (0, 0).
	for (std::size_t frame = 0; frame < 5; frame++) {
		block[ndp + 8 + 4 * frame] = static_cast<uint8>(12 + 16 * frame);
		block[ndp + 9 + 4 * frame] = 0x00;
		block[ndp + 10 + 4 * frame] = 0x0E;
		block[ndp + 11 + 4 * frame] = 0x00;
	}

	SIM_EXPECT(fixture.sim.HasArmedTransfer(0x02));
	SIM_EXPECT(fixture.sim.HostOut(0x02, block).IsOk());
	fixture.device.Process();

	NcmTest::Function::Counters counters = fixture.function.GetCounters();
	SIM_EXPECT_EQ(counters.framesReceived, 4);
	SIM_EXPECT_EQ(counters.framesDroppedReceive, 1);
	SIM_EXPECT_EQ(counters.ntbsMalformed, 0);
	SIM_EXPECT_EQ(events.blocks, 1);
	ExpectReceivedFrame(fixture, FrameA);
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


}


int main() {
	TestBlockWithTwoFramesIn();
	TestBlockWithTwoFramesOut();
	TestShortBlockHasNoZeroLengthPacket();
	TestWholePacketBlockHasZeroLengthPacket();
	TestQueueWaitsForBlockInFlight();
	TestSequenceRestartsWithSession();
	TestInputSizeLimitsBlocks();
	TestMalformedBlocksAreDropped();
	TestDeconfigureWithBlockInFlight();
	TestReceiveQueueFullDropsFrames();
	TestChainedNdpsDeliverAllFrames();
	TestZeroLengthOutTransferRearms();
	return SimTest::Finish("TestNcmData");
}
