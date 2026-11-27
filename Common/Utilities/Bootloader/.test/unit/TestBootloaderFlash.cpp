#include "Harness.h"
#include "TestRunner.h"

#include <string>
#include <vector>

using namespace Harness;


// The Finalize header must match the image; the reference CRC-32 is checked against the standard check value.
TEST_CASE(T08_FinalizeCorrectHeader) {
	ResetSystem();
	std::vector<uint8> checkBytes = AsBytes("123456789");
	CHECK_EQ(Crc32(checkBytes), 0xCBF43926u);

	Rig<PlainBootloader> rig(kFlashBase, kFlashSize);
	CHECK(rig.Init() == ResultStatus::ok);
	CHECK(rig.client.Erase());

	std::vector<uint8> image = MakeImage(1000, 0x50);
	CHECK(rig.client.Upload(image, 240, 0x01));

	Reply finalized = rig.client.Finalize(image);
	CHECK_EQ(finalized.status, kStatusSuccess);

	Reply verified = rig.client.Call(kCmdVerify);
	CHECK_EQ(verified.status, kStatusSuccess);

	std::vector<uint8> header = FlashRead(rig.flash, kHeaderAddress, 8);
	std::vector<uint8> expected = FirmwareHeaderBytes(Crc32(image), 1000);
	CHECK(header == expected);
}


// A wrong CRC answers ChecksumMismatch, the header is not written, and Verify fails.
TEST_CASE(T08_FinalizeWrongCrc) {
	ResetSystem();
	Rig<PlainBootloader> rig(kFlashBase, kFlashSize);
	CHECK(rig.Init() == ResultStatus::ok);
	CHECK(rig.client.Erase());

	std::vector<uint8> image = MakeImage(1000, 0x51);
	CHECK(rig.client.Upload(image, 240, 0x01));

	uint32 wrongCrc = Crc32(image) ^ 0x00000001u;
	Reply finalized = rig.client.FinalizeWith(wrongCrc, 1000);
	CHECK_EQ(finalized.status, kStatusChecksumMismatch);

	std::vector<uint8> header = FlashRead(rig.flash, kHeaderAddress, 8);
	std::vector<uint8> erasedHeader(8, 0xFF);
	CHECK(header == erasedHeader);

	Reply verified = rig.client.Call(kCmdVerify);
	CHECK_EQ(verified.status, kStatusError);
}


// Finalize with an empty payload is refused while requireFinalizeChecksum is true; no header is written.
TEST_CASE(T08_FinalizeEmptyPayload) {
	ResetSystem();
	Rig<PlainBootloader> rig(kFlashBase, kFlashSize);
	CHECK(rig.Init() == ResultStatus::ok);
	CHECK(rig.client.Erase());

	std::vector<uint8> image = MakeImage(1000, 0x52);
	CHECK(rig.client.Upload(image, 240, 0x01));

	Reply finalized = rig.client.Call(kCmdFinalize);
	CHECK_EQ(finalized.status, kStatusInvalidParameters);

	std::vector<uint8> header = FlashRead(rig.flash, kHeaderAddress, 8);
	std::vector<uint8> erasedHeader(8, 0xFF);
	CHECK(header == erasedHeader);
}


// With requireFinalizeChecksum = false (legacy host) an empty Finalize succeeds and Verify passes.
TEST_CASE(T08_FinalizeEmptyAllowedForLegacyHost) {
	ResetSystem();
	Rig<PlainBootloader> rig(kFlashBase, kFlashSize);
	CHECK(rig.Init() == ResultStatus::ok);
	rig.bootloader.requireFinalizeChecksum = false;
	CHECK(rig.client.Erase());

	std::vector<uint8> image = MakeImage(1000, 0x53);
	CHECK(rig.client.Upload(image, 240, 0x01));

	Reply finalized = rig.client.Call(kCmdFinalize);
	CHECK_EQ(finalized.status, kStatusSuccess);

	Reply verified = rig.client.Call(kCmdVerify);
	CHECK_EQ(verified.status, kStatusSuccess);
}


// A 10-byte Write is the final, unaligned chunk and succeeds; the next Write is misaligned and fails.
TEST_CASE(T18_WriteAlignmentTail) {
	ResetSystem();
	Rig<PlainBootloader> rig(kFlashBase, kFlashSize);
	rig.flash.requiredAlignment = 4;
	CHECK(rig.Init() == ResultStatus::ok);
	CHECK(rig.client.Erase());

	// 1001 bytes: four full 240-byte packets, then a 41-byte tail (not a multiple of 4).
	std::vector<uint8> image = MakeImage(1001, 0xB0);
	bool uploaded = rig.client.UploadChunks(image, { 240, 240, 240, 240, 41 }, 0x01);
	CHECK(uploaded);

	std::vector<uint8> stored = FlashRead(rig.flash, kFlashBase, image.size());
	CHECK(stored == image);
}


// Only the final chunk may be unaligned; a Write after an unaligned one answers Error.
TEST_CASE(T18_UnalignedWriteError) {
	ResetSystem();
	Rig<PlainBootloader> rig(kFlashBase, kFlashSize);
	rig.flash.requiredAlignment = 4;
	CHECK(rig.Init() == ResultStatus::ok);
	CHECK(rig.client.Erase());

	std::vector<uint8> tail = MakeImage(10, 0xC0);
	Reply first = rig.client.CallWithSequence(0x01, kCmdWrite, tail);
	CHECK_EQ(first.status, kStatusSuccess);

	std::vector<uint8> next = MakeImage(4, 0xC4);
	Reply second = rig.client.CallWithSequence(0x02, kCmdWrite, next);
	CHECK_EQ(second.status, kStatusError);
}


// A Write may end at the firmware header address but not reach into the 8 header bytes.
TEST_CASE(T19_WriteStopsBeforeHeader) {
	ResetSystem();
	Rig<PlainBootloader> rig(kFlashBase, kFlashSize);
	CHECK(rig.Init() == ResultStatus::ok);
	CHECK(rig.client.Erase());

	size_t usable = kFlashSize - 8;
	std::vector<uint8> image = MakeImage(usable, 0xD0);
	CHECK(rig.client.Upload(image, 240, 0x01));

	std::vector<uint8> beyond = MakeImage(4, 0xD4);
	Reply overflow = rig.client.CallWithSequence(0x20, kCmdWrite, beyond);
	CHECK_EQ(overflow.status, kStatusError);

	std::vector<uint8> header = FlashRead(rig.flash, kHeaderAddress, 8);
	std::vector<uint8> erasedHeader(8, 0xFF);
	CHECK(header == erasedHeader);
}


// RDP Level 1 (read-protected): by default the image can not be changed (Erase, Write, Finalize answer Protected) and Read
// answers Protected; nothing in flash changes.
TEST_CASE(T21_Rdp1RefusesUpdatesByDefault) {
	ResetSystem();
	Rig<PlainBootloader> rig(kFlashBase, kFlashSize);
	rig.flash.level = FakeFlash::FlashProtectionLevel::Level1;
	rig.flash.readProtected = true;
	std::vector<uint8> installed = MakeImage(64, 0xE8);
	std::copy(installed.begin(), installed.end(), rig.flash.memory.begin());
	CHECK(rig.Init() == ResultStatus::ok);

	Reply erase = rig.client.Call(kCmdErase);
	CHECK_EQ(erase.status, kStatusProtected);

	std::vector<uint8> image = MakeImage(16, 0xE0);
	Reply write = rig.client.CallWithSequence(0x01, kCmdWrite, image);
	CHECK_EQ(write.status, kStatusProtected);

	Reply finalized = rig.client.Finalize(image);
	CHECK_EQ(finalized.status, kStatusProtected);

	Reply read = rig.client.Call(kCmdRead);
	CHECK_EQ(read.status, kStatusProtected);

	std::vector<uint8> stored = FlashRead(rig.flash, kFlashBase, installed.size());
	CHECK(stored == installed);
}


// RDP Level 1 with allowUpdatesWhenReadProtected: Erase and Write work, Read still answers Protected.
TEST_CASE(T21_Rdp1UpdatesWhenAllowed) {
	ResetSystem();
	Rig<PlainBootloader> rig(kFlashBase, kFlashSize);
	rig.flash.level = FakeFlash::FlashProtectionLevel::Level1;
	rig.flash.readProtected = true;
	rig.bootloader.allowUpdatesWhenReadProtected = true;
	CHECK(rig.Init() == ResultStatus::ok);
	CHECK(rig.client.Erase());

	std::vector<uint8> image = MakeImage(240, 0xE0);
	CHECK(rig.client.Upload(image, 240, 0x01));

	std::vector<uint8> stored = FlashRead(rig.flash, kFlashBase, image.size());
	CHECK(stored == image);

	Reply read = rig.client.Call(kCmdRead);
	CHECK_EQ(read.status, kStatusProtected);
}


// RDP Level 2: the access level forbids flash changes, Initialize returns an error before the transport starts.
TEST_CASE(T21_Rdp2InitializeRefused) {
	ResetSystem();
	Rig<PlainBootloader> rig(kFlashBase, kFlashSize);
	rig.flash.level = FakeFlash::FlashProtectionLevel::Level2;
	rig.flash.readProtected = true;
	CHECK(rig.Init() == ResultStatus::accessError);
}


// FLASHBootloader write alignment: 8 (double-word flash) works end to end including the 8-byte header; values the header write
// can not honour, or a region that is not aligned, make Initialize fail.
TEST_CASE(T26_WriteAlignmentLimits) {
	ResetSystem();
	Rig<PlainBootloader> rig(kFlashBase, kFlashSize, 8u);
	rig.flash.requiredAlignment = 8;
	CHECK(rig.Init() == ResultStatus::ok);
	CHECK(rig.client.Erase());

	std::vector<uint8> image = MakeImage(1001, 0xF0);
	bool uploaded = rig.client.UploadChunks(image, { 240, 240, 240, 240, 41 }, 0x01);
	CHECK(uploaded);

	Reply finalized = rig.client.Finalize(image);
	CHECK_EQ(finalized.status, kStatusSuccess);

	Reply verified = rig.client.Call(kCmdVerify);
	CHECK_EQ(verified.status, kStatusSuccess);

	Rig<PlainBootloader> tooWide(kFlashBase, kFlashSize, 16u);
	CHECK(tooWide.Init() != ResultStatus::ok);

	Rig<PlainBootloader> notPowerOfTwo(kFlashBase, kFlashSize, 3u);
	CHECK(notPowerOfTwo.Init() != ResultStatus::ok);

	Rig<PlainBootloader> unalignedRegion(kFlashBase, kFlashSize - 4, 8u);
	CHECK(unalignedRegion.Init() != ResultStatus::ok);
}


// A Finalize repeated after a lost response answers Success and rewrites nothing. The flash cannot be programmed twice
// (requireErasedForProgram), so a second header write would fail.
TEST_CASE(T41_FinalizeRetryIsIdempotent) {
	ResetSystem();
	Rig<PlainBootloader> rig(kFlashBase, kFlashSize);
	rig.flash.requireErasedForProgram = true;
	CHECK(rig.Init() == ResultStatus::ok);
	CHECK(rig.client.Erase());

	std::vector<uint8> image = MakeImage(1000, 0x41);
	CHECK(rig.client.Upload(image, 240, 0x01));

	Reply first = rig.client.Finalize(image);
	CHECK_EQ(first.status, kStatusSuccess);

	Reply second = rig.client.Finalize(image);
	CHECK_EQ(second.status, kStatusSuccess);

	Reply verified = rig.client.Call(kCmdVerify);
	CHECK_EQ(verified.status, kStatusSuccess);

	std::vector<uint8> header = FlashRead(rig.flash, kHeaderAddress, 8);
	CHECK(header == FirmwareHeaderBytes(Crc32(image), 1000));
}


int main() {
	return TestRunner::RunAll("BootloaderFlash");
}
