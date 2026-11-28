#include "Harness.h"
#include "TestRunner.h"

#include <array>
#include <span>
#include <vector>

using namespace Harness;


// A length byte above PacketDataMaxSize is rejected without overrunning the packet object.
TEST_CASE(T01_PacketLengthOverrun) {
	ResetSystem();
	std::vector<uint8> oversized = OversizedPacket(0x42);

	{
		Rig<ProbeBootloader> probe(kFlashBase, kFlashSize);
		CHECK(probe.Init() == ResultStatus::ok);
		probe.comm.Feed(oversized);

		GuardedPacket guarded{};
		guarded.guard.fill(0xCC);
		probe.bootloader.ReadPacketProbe(guarded.packet);

		for (size_t i = 0; i < guarded.guard.size(); i++) {
			CHECK_EQ(guarded.guard[i], 0xCC);
		}
	}

	{
		Rig<PlainBootloader> rig(kFlashBase, kFlashSize);
		CHECK(rig.Init() == ResultStatus::ok);
		rig.comm.Feed(oversized);
		rig.comm.Feed(BuildCommand(0x07, kCmdPing));

		Reply rejected = rig.client.WaitReply();
		CHECK(rejected.valid);
		CHECK_EQ(rejected.sequence, 0x42);
		CHECK_EQ(rejected.status, kStatusInvalidParameters);

		Reply pong = rig.client.WaitReply();
		CHECK(pong.valid);
		CHECK_EQ(pong.sequence, 0x07);
		CHECK_EQ(pong.status, kStatusSuccess);
	}
}


// A partial packet is dropped once packetTimeoutMs has passed since it reached the front of the buffer; the next command is
// answered. The default is 10000 ms.
TEST_CASE(T02_PartialPacketTimeout) {
	ResetSystem();
	Rig<PlainBootloader> rig(kFlashBase, kFlashSize);
	CHECK(rig.Init() == ResultStatus::ok);
	CHECK_EQ(rig.bootloader.packetTimeoutMs, 10000u);
	rig.bootloader.packetTimeoutMs = 2000;

	// Write header announcing 16 data bytes; the data never arrives. The header reaches the front at t = 1000.
	std::vector<uint8> partial = { 0xAA, 0x05, kCmdWrite, 0x10 };
	rig.comm.Feed(partial);
	rig.bootloader.Process();
	CHECK(!rig.comm.HasUnread());

	VhalSim::SetMs(2000);
	rig.bootloader.Process();
	CHECK(!rig.comm.HasUnread());

	VhalSim::SetMs(3500);
	rig.bootloader.Process();
	CHECK(!rig.comm.HasUnread());

	Reply pong = rig.client.CallWithSequence(0x06, kCmdPing);
	CHECK(pong.valid);
	CHECK_EQ(pong.sequence, 0x06);
	CHECK_EQ(pong.status, kStatusSuccess);
}


// An empty span means "transport lost framing"; it must not abort the async erase.
TEST_CASE(T03_EmptySpanKeepsErase) {
	ResetSystem();
	Rig<PlainBootloader> rig(kFlashBase, kFlashSize);
	CHECK(rig.Init() == ResultStatus::ok);
	FillFlash(rig.flash, 0x00);

	rig.client.Send(0x09, kCmdErase, std::span<const uint8>());
	rig.comm.Feed(std::span<const uint8>());

	Reply erased = rig.client.WaitReply();
	CHECK(erased.valid);
	CHECK_EQ(erased.sequence, 0x09);
	CHECK_EQ(erased.status, kStatusSuccess);

	std::vector<uint8> flashContent = FlashRead(rig.flash, kFlashBase, kFlashSize);
	std::vector<uint8> erasedContent(kFlashSize, 0xFF);
	CHECK(flashContent == erasedContent);
}


// Sequence 0xFF is an ordinary sequence number; the first Write after Erase is stored.
TEST_CASE(T04_FirstWriteSequence0xFF) {
	ResetSystem();
	Rig<PlainBootloader> rig(kFlashBase, kFlashSize);
	CHECK(rig.Init() == ResultStatus::ok);
	CHECK(rig.client.Erase());

	std::vector<uint8> image = MakeImage(100, 0x10);
	Reply written = rig.client.CallWithSequence(0xFF, kCmdWrite, image);
	CHECK(written.valid);
	CHECK_EQ(written.sequence, 0xFF);
	CHECK_EQ(written.status, kStatusSuccess);

	std::vector<uint8> stored = FlashRead(rig.flash, kFlashBase, image.size());
	CHECK(stored == image);

	// Finalize checks bytesWritten against the expected size, so it proves the stream counted 100 bytes.
	Reply finalized = rig.client.Finalize(image);
	CHECK_EQ(finalized.status, kStatusSuccess);
}


// A retry with the same sequence after a successful Write is answered Success and not written again.
TEST_CASE(T05_WriteRetryNotWrittenTwice) {
	ResetSystem();
	Rig<PlainBootloader> rig(kFlashBase, kFlashSize);
	CHECK(rig.Init() == ResultStatus::ok);
	CHECK(rig.client.Erase());

	std::vector<uint8> chunk = MakeImage(240, 0x20);
	Reply first = rig.client.CallWithSequence(0x03, kCmdWrite, chunk);
	CHECK_EQ(first.status, kStatusSuccess);

	uint32 writesBefore = rig.flash.writeCalls;
	Reply retry = rig.client.CallWithSequence(0x03, kCmdWrite, chunk);
	CHECK_EQ(retry.status, kStatusSuccess);
	CHECK_EQ(rig.flash.writeCalls, writesBefore);

	Reply status = rig.client.Call(kCmdGetStatus);
	CHECK_EQ(status.status, kStatusSuccess);
	CHECK_EQ(status.data.size(), 8u);
	CHECK_EQ(ReadLe32(status.data, 4), 240u);
}


// After a failed flash write every Write answers the same error until Erase; a full upload works after Erase.
TEST_CASE(T06_FlashWriteFailureIsSticky) {
	ResetSystem();
	Rig<PlainBootloader> rig(kFlashBase, kFlashSize);
	CHECK(rig.Init() == ResultStatus::ok);
	CHECK(rig.client.Erase());

	rig.flash.failWrites = 1;
	std::vector<uint8> chunk = MakeImage(240, 0x30);

	Reply failed = rig.client.CallWithSequence(0x01, kCmdWrite, chunk);
	CHECK_EQ(failed.status, kStatusError);

	Reply retry = rig.client.CallWithSequence(0x01, kCmdWrite, chunk);
	CHECK_EQ(retry.status, kStatusError);

	Reply nextChunk = rig.client.CallWithSequence(0x02, kCmdWrite, chunk);
	CHECK_EQ(nextChunk.status, kStatusError);

	CHECK(rig.client.Erase());

	std::vector<uint8> image = MakeImage(1000, 0x31);
	CHECK(rig.client.Upload(image, 240, 0x01));

	Reply finalized = rig.client.Finalize(image);
	CHECK_EQ(finalized.status, kStatusSuccess);

	Reply verified = rig.client.Call(kCmdVerify);
	CHECK_EQ(verified.status, kStatusSuccess);
}


// ResetWritePos restarts the stream; the size of the image sent after the reset is what Finalize records.
TEST_CASE(T07_ResetWritePosReupload) {
	ResetSystem();
	Rig<PlainBootloader> rig(kFlashBase, kFlashSize);
	CHECK(rig.Init() == ResultStatus::ok);
	CHECK(rig.client.Erase());

	std::vector<uint8> imageA = MakeImage(1000, 0x40);
	CHECK(rig.client.Upload(imageA, 240, 0x01));

	// The last Write sent ahead of the reset used sequence 5; the first Write after the reset reuses 5 and must still be processed.
	Reply reset = rig.client.Call(kCmdResetWritePos);
	CHECK_EQ(reset.status, kStatusSuccess);

	// A different image that is not a prefix of imageA. The flash is erased directly (as an erase of the region would leave it),
	// so any leftover bytes of imageA would show up in the result.
	rig.flash.Erase(kFlashBase, kFlashSize);
	std::vector<uint8> imageB = MakeImage(500, 0x41);
	CHECK(!std::equal(imageB.begin(), imageB.end(), imageA.begin()));
	bool uploaded = rig.client.UploadChunks(imageB, { 240, 240, 20 }, 0x05);
	CHECK(uploaded);

	std::vector<uint8> stored = FlashRead(rig.flash, kFlashBase, imageB.size());
	CHECK(stored == imageB);

	Reply finalized = rig.client.Finalize(imageB);
	CHECK_EQ(finalized.status, kStatusSuccess);

	Reply verified = rig.client.Call(kCmdVerify);
	CHECK_EQ(verified.status, kStatusSuccess);

	std::vector<uint8> header = FlashRead(rig.flash, kHeaderAddress, 8);
	CHECK_EQ(ReadLe32(header, 4), 500u);
}


// GetStatus returns 8 bytes: state (Idle = 0), three zero reserved bytes, bytesReceived.
TEST_CASE(T10_GetStatusLayout) {
	ResetSystem();
	Rig<PlainBootloader> rig(kFlashBase, kFlashSize);
	CHECK(rig.Init() == ResultStatus::ok);
	CHECK(rig.client.Erase());

	std::vector<uint8> image = MakeImage(100, 0x10);
	CHECK(rig.client.Upload(image, 100, 0x01));

	Reply status = rig.client.Call(kCmdGetStatus);
	CHECK_EQ(status.status, kStatusSuccess);
	CHECK_EQ(status.data.size(), 8u);
	CHECK_EQ(status.data[0], 0x00);
	CHECK_EQ(status.data[1], 0x00);
	CHECK_EQ(status.data[2], 0x00);
	CHECK_EQ(status.data[3], 0x00);
	CHECK_EQ(ReadLe32(status.data, 4), 100u);
}


// A decryption hook that returns an empty span fails the Write with CryptoError, not Error.
TEST_CASE(T11_DecryptionFailureCryptoError) {
	ResetSystem();
	Rig<DecryptFailBootloader> rig(kFlashBase, kFlashSize);
	CHECK(rig.Init() == ResultStatus::ok);
	CHECK(rig.client.Erase());

	std::vector<uint8> chunk = MakeImage(240, 0x70);
	Reply reply = rig.client.Call(kCmdWrite, chunk);
	CHECK_EQ(reply.status, kStatusCryptoError);
}


// An erase iteration returning a status other than ok/inProgress answers Error and returns to Idle.
TEST_CASE(T12_EraseUnexpectedStatus) {
	ResetSystem();
	Rig<PlainBootloader> rig(kFlashBase, kFlashSize);
	CHECK(rig.Init() == ResultStatus::ok);

	rig.bootloader.eraseStatusOverride = ResultStatus::timeout;
	rig.client.Send(0x08, kCmdErase, std::span<const uint8>());

	Reply erased = rig.client.WaitReply();
	CHECK(erased.valid);
	CHECK_EQ(erased.sequence, 0x08);
	CHECK_EQ(erased.status, kStatusError);

	Reply pong = rig.client.CallWithSequence(0x09, kCmdPing);
	CHECK_EQ(pong.status, kStatusSuccess);
}


// A response refused by a busy transport is kept and sent later; no response is lost or skipped.
TEST_CASE(T13_TransportBusyNoLoss) {
	ResetSystem();
	Rig<PlainBootloader> rig(kFlashBase, kFlashSize);
	CHECK(rig.Init() == ResultStatus::ok);
	CHECK(rig.client.Erase());

	std::vector<uint8> image = MakeImage(480, 0x80);
	CHECK(rig.client.Upload(image, 240, 0x01));

	rig.comm.busyResponses = 1;
	rig.client.Send(0x02, kCmdRead, std::span<const uint8>());
	CHECK(!rig.comm.HasUnread());
	CHECK_EQ(rig.comm.busyCount, 1);

	Reply first = rig.client.WaitReply();
	CHECK(first.valid);
	CHECK_EQ(first.sequence, 0x02);
	CHECK_EQ(first.status, kStatusSuccess);

	std::vector<uint8> expectedFirst(image.begin(), image.begin() + 240);
	CHECK(first.data == expectedFirst);

	Reply second = rig.client.CallWithSequence(0x03, kCmdRead);
	CHECK_EQ(second.status, kStatusSuccess);

	std::vector<uint8> expectedSecond(image.begin() + 240, image.end());
	CHECK(second.data == expectedSecond);
	CHECK(!rig.comm.HasUnread());
}


// Start answers Success first; OnStartApplication runs only after the 150 ms flush delay.
TEST_CASE(T14_StartDelayed) {
	ResetSystem();
	Rig<PlainBootloader> rig(kFlashBase, kFlashSize);
	CHECK(rig.Init() == ResultStatus::ok);
	CHECK(rig.client.Erase());

	std::vector<uint8> image = MakeImage(1000, 0x70);
	CHECK(rig.client.Upload(image, 240, 0x01));

	Reply finalized = rig.client.Finalize(image);
	CHECK_EQ(finalized.status, kStatusSuccess);

	Reply started = rig.client.Call(kCmdStart);
	CHECK_EQ(started.status, kStatusSuccess);
	CHECK_EQ(rig.bootloader.startCount, 0u);

	VhalSim::AdvanceMs(100);
	rig.bootloader.Process();
	CHECK_EQ(rig.bootloader.startCount, 0u);

	VhalSim::AdvanceMs(1000);
	rig.bootloader.Process();
	CHECK_EQ(rig.bootloader.startCount, 1u);
}


// The timeout runs from when the stale header reached the front, so steady traffic behind it does not keep it alive. Pings every
// 500 ms are held back until packetTimeoutMs (2000 ms after t = 1000) has passed, then answered.
TEST_CASE(T27_StaleHeaderWithSteadyTraffic) {
	ResetSystem();
	Rig<PlainBootloader> rig(kFlashBase, kFlashSize);
	CHECK(rig.Init() == ResultStatus::ok);
	rig.bootloader.packetTimeoutMs = 2000;

	// Header claims 240 data bytes (0xF0); the data never arrives. It reaches the front at t = 1000.
	std::vector<uint8> stale = { 0xAA, 0x05, kCmdWrite, 0xF0 };
	rig.comm.Feed(stale);
	rig.bootloader.Process();

	Reply firstReply;
	uint64 firstReplyMs = 0;

	for (uint32 step = 1; step <= 10 && !firstReply.valid; step++) {
		VhalSim::SetMs(1000 + 500 * static_cast<uint64>(step));
		rig.comm.Feed(BuildCommand(static_cast<uint8>(0x10 + step), kCmdPing));
		rig.bootloader.Process();

		if (VhalSim::clockMs <= 3000) {
			CHECK(!rig.comm.HasUnread());
		}

		if (rig.comm.HasUnread()) {
			firstReply = rig.comm.TakeReply();
			firstReplyMs = VhalSim::clockMs;
		}
	}

	CHECK(firstReply.valid);
	CHECK(firstReplyMs > 3000);
	CHECK(firstReplyMs <= 3500);
	CHECK_EQ(firstReply.sequence, 0x11);
	CHECK_EQ(firstReply.status, kStatusSuccess);
}


// The timeout is per packet, not per byte. A 244-byte Write arriving in 16-byte pieces 400 ms apart takes 6000 ms, below the
// default 10000 ms, and is processed normally.
TEST_CASE(T28_SlowPacketNotDropped) {
	ResetSystem();
	Rig<PlainBootloader> rig(kFlashBase, kFlashSize);
	CHECK(rig.Init() == ResultStatus::ok);
	CHECK(rig.client.Erase());

	std::vector<uint8> image = MakeImage(240, 0x28);
	std::vector<uint8> packet = BuildCommand(0x02, kCmdWrite, image);
	CHECK_EQ(packet.size(), 244u);

	size_t offset = 0;

	while (offset < packet.size()) {
		size_t part = std::min<size_t>(16, packet.size() - offset);
		rig.comm.Feed(std::span<const uint8>(packet.data() + offset, part));
		rig.bootloader.Process();
		offset += part;
		VhalSim::AdvanceMs(400);
	}

	Reply written = rig.client.WaitReply();
	CHECK(written.valid);
	CHECK_EQ(written.sequence, 0x02);
	CHECK_EQ(written.status, kStatusSuccess);

	std::vector<uint8> stored = FlashRead(rig.flash, kFlashBase, image.size());
	CHECK(stored == image);
}


// A reset (empty span) drops a queued Read answer. The host re-sends the Read and gets the same data, because the read position
// only advances once an answer is actually sent.
TEST_CASE(T29_QueuedReadSurvivesTransportReset) {
	ResetSystem();
	Rig<PlainBootloader> rig(kFlashBase, kFlashSize);
	CHECK(rig.Init() == ResultStatus::ok);
	CHECK(rig.client.Erase());

	std::vector<uint8> image = MakeImage(100, 0x29);
	CHECK(rig.client.Upload(image, 100, 0x01));

	// The transport is busy, so the Read answer waits in the pending slot.
	rig.comm.busyResponses = 1;
	rig.client.Send(0x02, kCmdRead, std::span<const uint8>());
	CHECK(!rig.comm.HasUnread());
	CHECK_EQ(rig.comm.busyCount, 1);

	// The transport loses framing: the queued answer is dropped.
	rig.comm.Feed(std::span<const uint8>());

	Reply read = rig.client.CallWithSequence(0x03, kCmdRead);
	CHECK(read.valid);
	CHECK_EQ(read.sequence, 0x03);
	CHECK_EQ(read.status, kStatusSuccess);
	CHECK(read.data == image);
	CHECK(!rig.comm.HasUnread());
}


// Start answers Busy while an erase is running; OnStartApplication is not called. Once the erase has completed, Start works as
// usual.
TEST_CASE(T30_StartBusyDuringErase) {
	ResetSystem();
	Rig<PlainBootloader> rig(kFlashBase, kFlashSize);
	CHECK(rig.Init() == ResultStatus::ok);
	rig.bootloader.checkFirmware = false;
	FillFlash(rig.flash, 0x00);

	rig.client.Send(0x02, kCmdErase, std::span<const uint8>());
	CHECK(rig.bootloader.eraseIterations < 4);

	Reply busy = rig.client.CallWithSequence(0x03, kCmdStart);
	CHECK(busy.valid);
	CHECK_EQ(busy.sequence, 0x03);
	CHECK_EQ(busy.status, kStatusBusy);

	Reply erased = rig.client.WaitReply();
	CHECK(erased.valid);
	CHECK_EQ(erased.sequence, 0x02);
	CHECK_EQ(erased.status, kStatusSuccess);
	CHECK_EQ(rig.bootloader.startCount, 0u);

	Reply started = rig.client.CallWithSequence(0x04, kCmdStart);
	CHECK_EQ(started.status, kStatusSuccess);

	VhalSim::AdvanceMs(1000);
	rig.bootloader.Process();
	CHECK_EQ(rig.bootloader.startCount, 1u);
}


// Finalize during an erase answers Busy: a header written during the erase would be erased again, or would mark a half-erased region valid.
// The erase still completes and leaves the header erased.
TEST_CASE(T45_FinalizeBusyDuringErase) {
	ResetSystem();
	Rig<PlainBootloader> rig(kFlashBase, kFlashSize);
	CHECK(rig.Init() == ResultStatus::ok);
	rig.bootloader.requireFinalizeChecksum = false;
	FillFlash(rig.flash, 0x00);

	rig.client.Send(0x02, kCmdErase, std::span<const uint8>());
	CHECK(rig.bootloader.eraseIterations < 4);

	Reply busy = rig.client.CallWithSequence(0x03, kCmdFinalize);
	CHECK(busy.valid);
	CHECK_EQ(busy.sequence, 0x03);
	CHECK_EQ(busy.status, kStatusBusy);

	Reply erased = rig.client.WaitReply();
	CHECK(erased.valid);
	CHECK_EQ(erased.sequence, 0x02);
	CHECK_EQ(erased.status, kStatusSuccess);

	std::vector<uint8> header = FlashRead(rig.flash, kHeaderAddress, 8);
	std::vector<uint8> erasedHeader(8, 0xFF);
	CHECK(header == erasedHeader);
}


// Read at an offset (retry-safe): a 4-byte little-endian offset reads from that position and does not move the streaming position.
// An offset at or past the end answers EndOfData; a payload of any other length is refused.
TEST_CASE(T40_ReadAtOffset) {
	ResetSystem();
	Rig<PlainBootloader> rig(kFlashBase, kFlashSize);
	CHECK(rig.Init() == ResultStatus::ok);
	CHECK(rig.client.Erase());

	std::vector<uint8> image = MakeImage(600, 0x40);
	CHECK(rig.client.Upload(image, 240, 0x01));

	std::vector<uint8> offset240 = LittleEndian32(240);
	Reply middle = rig.client.Call(kCmdRead, offset240);
	CHECK_EQ(middle.status, kStatusSuccess);
	std::vector<uint8> expectedMiddle(image.begin() + 240, image.begin() + 480);
	CHECK(middle.data == expectedMiddle);

	std::vector<uint8> offset480 = LittleEndian32(480);
	Reply tail = rig.client.Call(kCmdRead, offset480);
	CHECK_EQ(tail.status, kStatusSuccess);
	std::vector<uint8> expectedTail(image.begin() + 480, image.end());
	CHECK_EQ(tail.data.size(), 120u);
	CHECK(tail.data == expectedTail);

	// The same offset again returns the same bytes.
	Reply again = rig.client.Call(kCmdRead, offset240);
	CHECK_EQ(again.status, kStatusSuccess);
	CHECK(again.data == expectedMiddle);

	std::vector<uint8> offset600 = LittleEndian32(600);
	Reply past = rig.client.Call(kCmdRead, offset600);
	CHECK_EQ(past.status, kStatusEndOfData);

	std::vector<uint8> threeBytes = { 0x00, 0x00, 0x00 };
	Reply malformed = rig.client.Call(kCmdRead, threeBytes);
	CHECK_EQ(malformed.status, kStatusInvalidParameters);

	// The streaming form (no payload) still starts at byte 0: the offset reads did not move its position.
	Reply streamed = rig.client.Call(kCmdRead);
	CHECK_EQ(streamed.status, kStatusSuccess);
	std::vector<uint8> expectedHead(image.begin(), image.begin() + 240);
	CHECK(streamed.data == expectedHead);
}


// GetInfo: the 24-byte payload carries the packet data size at byte 22 (240, the default PacketDataMaxSize) and the cipher id at
// byte 23: 0 = none, 1 = AES-ECB, 2 = AES-CBC, 3 = AES-CTR.
TEST_CASE(T44_GetInfoReportsCipherAndPacketSize) {
	ResetSystem();
	std::array<uint8, 16> key = TestKey();
	std::array<uint8, 16> iv = TestIv();

	{
		Rig<PlainBootloader> rig(kFlashBase, kFlashSize);
		CHECK(rig.Init() == ResultStatus::ok);

		Reply info = rig.client.Call(kCmdGetInfo);
		CHECK_EQ(info.status, kStatusSuccess);
		CHECK_EQ(info.data.size(), 24u);
		CHECK_EQ(info.data[22], 240);
		CHECK_EQ(info.data[23], 0);
	}

	{
		Rig<AesBootloader> rig(AESMode::ECB, key, kFlashBase, kFlashSize, iv);
		CHECK(rig.Init() == ResultStatus::ok);

		Reply info = rig.client.Call(kCmdGetInfo);
		CHECK_EQ(info.status, kStatusSuccess);
		CHECK_EQ(info.data.size(), 24u);
		CHECK_EQ(info.data[22], 240);
		CHECK_EQ(info.data[23], 1);
	}

	{
		Rig<AesBootloader> rig(AESMode::CBC, key, kFlashBase, kFlashSize, iv);
		CHECK(rig.Init() == ResultStatus::ok);

		Reply info = rig.client.Call(kCmdGetInfo);
		CHECK_EQ(info.status, kStatusSuccess);
		CHECK_EQ(info.data.size(), 24u);
		CHECK_EQ(info.data[22], 240);
		CHECK_EQ(info.data[23], 2);
	}

	{
		Rig<AesBootloader> rig(AESMode::CTR, key, kFlashBase, kFlashSize, iv);
		CHECK(rig.Init() == ResultStatus::ok);

		Reply info = rig.client.Call(kCmdGetInfo);
		CHECK_EQ(info.status, kStatusSuccess);
		CHECK_EQ(info.data.size(), 24u);
		CHECK_EQ(info.data[22], 240);
		CHECK_EQ(info.data[23], 3);
	}
}


int main() {
	return TestRunner::RunAll("BootloaderProtocol");
}
