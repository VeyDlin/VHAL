#include "Harness.h"
#include <TestRunner.h>

#include <array>
#include <vector>

using namespace Harness;


// CTR is one continuous stream over the image; packets of 240/240/240/200/80 bytes decrypt to the plaintext.
TEST_CASE(T15_CtrContinuousStream) {
	ResetSystem();

	// Known-answer check of the block primitive the reference relies on (FIPS-197 Appendix B).
	std::array<uint8, 16> key = TestKey();
	std::array<uint8, 16> block = { 0x32, 0x43, 0xf6, 0xa8, 0x88, 0x5a, 0x30, 0x8d, 0x31, 0x31, 0x98, 0xa2, 0xe0, 0x37, 0x07, 0x34 };
	std::array<uint8, 16> expectedBlock = { 0x39, 0x25, 0x84, 0x1d, 0x02, 0xdc, 0x09, 0xfb, 0xdc, 0x11, 0x85, 0x97, 0x19, 0x6a, 0x0b, 0x32 };
	AES128 primitive(key);
	primitive.EncryptECB(block);
	CHECK(block == expectedBlock);

	// IV whose low counter bytes carry into the next byte within this image.
	std::array<uint8, 16> iv = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFC };
	std::vector<uint8> image = MakeImage(1000, 0x90);
	std::vector<uint8> ciphertext = CtrReference(key, iv, image);

	// The library's own CTR must agree with the independent reference (standard 128-bit big-endian counter).
	std::vector<uint8> libraryCiphertext = image;
	AES128 library(key);
	library.SetIV(iv);
	library.EncryptCTR(std::span<uint8>(libraryCiphertext.data(), libraryCiphertext.size()));
	CHECK(libraryCiphertext == ciphertext);

	Rig<AesBootloader> rig(AESMode::CTR, key, kFlashBase, kFlashSize, iv);
	CHECK(rig.Init() == ResultStatus::ok);
	CHECK(rig.client.Erase());

	bool uploaded = rig.client.UploadChunks(ciphertext, { 240, 240, 240, 200, 80 }, 0x01);
	CHECK(uploaded);

	std::vector<uint8> stored = FlashRead(rig.flash, kFlashBase, image.size());
	CHECK(stored == image);

	Reply finalized = rig.client.Finalize(image);
	CHECK_EQ(finalized.status, kStatusSuccess);

	Reply verified = rig.client.Call(kCmdVerify);
	CHECK_EQ(verified.status, kStatusSuccess);
}


// 1000 bytes of CBC ciphertext leave an 8-byte partial block in the accumulator; Finalize answers Error.
TEST_CASE(T09_FinalizeIncompleteCipherBlock) {
	ResetSystem();
	std::array<uint8, 16> key = TestKey();
	std::array<uint8, 16> iv = TestIv();

	Rig<AesBootloader> rig(AESMode::CBC, key, kFlashBase, kFlashSize, iv);
	CHECK(rig.Init() == ResultStatus::ok);
	CHECK(rig.client.Erase());

	std::vector<uint8> ciphertext = MakeImage(1000, 0x60);
	CHECK(rig.client.Upload(ciphertext, 240, 0x01));

	Reply finalized = rig.client.Finalize(ciphertext);
	CHECK_EQ(finalized.status, kStatusError);
}


// The CBC chain restarts on Erase, so the same ciphertext uploads correctly twice in one session.
TEST_CASE(T16_CbcChainRestartedByErase) {
	ResetSystem();
	std::array<uint8, 16> key = TestKey();
	std::array<uint8, 16> iv = TestIv();
	std::vector<uint8> image = MakeImage(1008, 0xA0);
	std::vector<uint8> ciphertext = CbcEncryptReference(key, iv, image);

	Rig<AesBootloader> rig(AESMode::CBC, key, kFlashBase, kFlashSize, iv);
	CHECK(rig.Init() == ResultStatus::ok);

	CHECK(rig.client.Erase());
	bool firstUpload = rig.client.Upload(ciphertext, 240, 0x01);
	CHECK(firstUpload);
	std::vector<uint8> firstStored = FlashRead(rig.flash, kFlashBase, image.size());
	CHECK(firstStored == image);

	CHECK(rig.client.Erase());
	bool secondUpload = rig.client.Upload(ciphertext, 240, 0x01);
	CHECK(secondUpload);
	std::vector<uint8> secondStored = FlashRead(rig.flash, kFlashBase, image.size());
	CHECK(secondStored == image);
}


// Read-back is disabled in encrypted mode; Read answers Protected even with data present.
TEST_CASE(T17_AesReadAnswersProtected) {
	ResetSystem();
	std::array<uint8, 16> key = TestKey();
	std::array<uint8, 16> iv = TestIv();

	Rig<AesBootloader> rig(AESMode::CTR, key, kFlashBase, kFlashSize, iv);
	CHECK(rig.Init() == ResultStatus::ok);
	CHECK(rig.client.Erase());

	std::vector<uint8> ciphertext = MakeImage(240, 0x17);
	CHECK(rig.client.Upload(ciphertext, 240, 0x01));

	Reply read = rig.client.Call(kCmdRead);
	CHECK_EQ(read.status, kStatusProtected);
}


// A key of the wrong size makes Initialize fail; the correct size initialises.
TEST_CASE(T20_WrongKeySizeInitializeFails) {
	ResetSystem();
	std::array<uint8, 16> iv = TestIv();

	std::vector<uint8> shortKey(15, 0x11);
	Rig<AesBootloader> badRig(AESMode::CTR, shortKey, kFlashBase, kFlashSize, iv);
	CHECK(badRig.Init() == ResultStatus::invalidParameter);

	std::array<uint8, 16> key = TestKey();
	Rig<AesBootloader> goodRig(AESMode::CTR, key, kFlashBase, kFlashSize, iv);
	CHECK(goodRig.Init() == ResultStatus::ok);
}


// SetIV: a per-image IV sent with SetIV replaces the construction IV. The image is one CTR stream under this IV, which the
// construction IV alone would not decrypt.
TEST_CASE(T42_SetIvChangesKeystream) {
	ResetSystem();
	std::array<uint8, 16> key = TestKey();
	std::array<uint8, 16> ivA = TestIv();
	std::array<uint8, 16> ivB = SecondIv();
	std::vector<uint8> image = MakeImage(1000, 0xB0);
	std::vector<uint8> ciphertext = CtrReference(key, ivB, image);

	// The test only means something if the two IVs give different keystreams for this image.
	CHECK(CtrReference(key, ivA, image) != ciphertext);

	Rig<AesBootloader> rig(AESMode::CTR, key, kFlashBase, kFlashSize, ivA);
	CHECK(rig.Init() == ResultStatus::ok);

	Reply setIv = rig.client.Call(kCmdSetIv, ivB);
	CHECK_EQ(setIv.status, kStatusSuccess);
	CHECK(rig.client.Erase());

	bool uploaded = rig.client.UploadChunks(ciphertext, { 240, 240, 240, 200, 80 }, 0x01);
	CHECK(uploaded);

	std::vector<uint8> stored = FlashRead(rig.flash, kFlashBase, image.size());
	CHECK(stored == image);

	Reply finalized = rig.client.Finalize(image);
	CHECK_EQ(finalized.status, kStatusSuccess);
}


// SetIV rules: plain and ECB bootloaders answer InvalidCommand; CBC needs a 16-byte IV; an erase in progress answers Busy;
// read-out protection (Level 1, default policy) answers Protected.
TEST_CASE(T43_SetIvRules) {
	ResetSystem();
	std::array<uint8, 16> key = TestKey();
	std::array<uint8, 16> iv = TestIv();
	std::array<uint8, 16> newIv = SecondIv();

	{
		Rig<PlainBootloader> rig(kFlashBase, kFlashSize);
		CHECK(rig.Init() == ResultStatus::ok);

		Reply reply = rig.client.Call(kCmdSetIv, newIv);
		CHECK_EQ(reply.status, kStatusInvalidCommand);
	}

	{
		Rig<AesBootloader> rig(AESMode::ECB, key, kFlashBase, kFlashSize, iv);
		CHECK(rig.Init() == ResultStatus::ok);

		Reply reply = rig.client.Call(kCmdSetIv, newIv);
		CHECK_EQ(reply.status, kStatusInvalidCommand);
	}

	{
		Rig<AesBootloader> rig(AESMode::CBC, key, kFlashBase, kFlashSize, iv);
		CHECK(rig.Init() == ResultStatus::ok);

		std::vector<uint8> shortIv(15, 0x11);
		Reply reply = rig.client.Call(kCmdSetIv, shortIv);
		CHECK_EQ(reply.status, kStatusInvalidParameters);
	}

	{
		Rig<AesBootloader> rig(AESMode::CTR, key, kFlashBase, kFlashSize, iv);
		CHECK(rig.Init() == ResultStatus::ok);
		rig.bootloader.checkFirmware = false;
		FillFlash(rig.flash, 0x00);

		rig.client.Send(0x02, kCmdErase, std::span<const uint8>());
		CHECK(rig.bootloader.eraseIterations < 4);

		Reply busy = rig.client.CallWithSequence(0x03, kCmdSetIv, newIv);
		CHECK(busy.valid);
		CHECK_EQ(busy.sequence, 0x03);
		CHECK_EQ(busy.status, kStatusBusy);

		Reply erased = rig.client.WaitReply();
		CHECK(erased.valid);
		CHECK_EQ(erased.sequence, 0x02);
		CHECK_EQ(erased.status, kStatusSuccess);
	}

	{
		Rig<AesBootloader> rig(AESMode::CTR, key, kFlashBase, kFlashSize, iv);
		rig.flash.level = FakeFlash::FlashProtectionLevel::Level1;
		rig.flash.readProtected = true;
		CHECK(rig.Init() == ResultStatus::ok);

		Reply reply = rig.client.Call(kCmdSetIv, newIv);
		CHECK_EQ(reply.status, kStatusProtected);
	}
}


int main() {
	TestRunner::RunAll();
	return VhalCheck::Finish("AesBootloader");
}
