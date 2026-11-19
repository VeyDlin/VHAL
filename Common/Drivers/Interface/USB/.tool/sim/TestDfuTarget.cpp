#include <VHAL.h>
#include "DfuTestDevices.h"
#include "SimAssert.h"
#include <Drivers/Interface/USB/Class/DFU/DfuFlashTarget.h>
#include <Drivers/Interface/USB/Class/DFU/DfuFunction.h>
#include <Drivers/Interface/USB/Class/DFU/IDfuTarget.h>
#include <Utilities/Result/Result.h>
#include <array>
#include <cstddef>
#include <cstring>
#include <span>
#include <vector>


// The example target over a FLASH adapter, checked against a fake flash with the adapter's methods and a 1 KiB region of 256-byte
// pages. Covers the erase as data arrives, the 8-byte units and the padded last unit, the verify after each unit, and the status
// code each failure reports.


namespace {


constexpr uint32 Key1 = 0x45670123;
constexpr uint32 Key2 = 0xCDEF89AB;
constexpr uint32 RegionSize = 1024;
constexpr uint32 PageSize = 256;


// A flash of RegionSize bytes, erased to 0xFF. The failure switches make each adapter call report what a failing part reports.
class FakeFlash {
public:
	std::array<uint8, RegionSize> memory{};
	std::vector<uint32> erasedPages;
	uint32 unlockCount = 0;
	uint32 lockCount = 0;
	bool locked = true;
	bool unlockFails = false;
	bool eraseFails = false;
	bool eraseKeepsData = false;
	bool programFails = false;
	bool corruptWrites = false;


	FakeFlash() {
		memory.fill(0xFF);
	}


	ResultStatus Unlock(uint32 key1, uint32 key2) {
		if (unlockFails || key1 != Key1 || key2 != Key2) {
			return ResultStatus::accessError;
		}

		unlockCount++;
		locked = false;
		return ResultStatus::ok;
	}


	ResultStatus Lock() {
		lockCount++;
		locked = true;
		return ResultStatus::ok;
	}


	Result<uint8> Read(uint8* address) {
		return Result<uint8>(Ok<uint8>(*address));
	}


	ResultStatus WriteData(uint32* address, const void* data, std::size_t size) {
		if (programFails) {
			return ResultStatus::writeError;
		}

		uint32 offset = static_cast<uint32>(reinterpret_cast<uint8*>(address) - memory.data());
		const uint8* source = static_cast<const uint8*>(data);
		for (std::size_t index = 0; index < size; index++) {
			memory[offset + index] = source[index];
		}

		if (corruptWrites) {
			memory[offset + size - 1] ^= 0x01;
		}

		return ResultStatus::ok;
	}


	ResultStatus PageErase(uint8* address) {
		uint32 offset = static_cast<uint32>(address - memory.data());
		erasedPages.push_back(offset);
		if (eraseFails) {
			return ResultStatus::busError;
		}

		if (!eraseKeepsData) {
			std::memset(memory.data() + offset, 0xFF, PageSize);
		}

		return ResultStatus::ok;
	}
};


std::vector<uint8> Pattern(std::size_t count, uint8 start) {
	std::vector<uint8> bytes(count);
	for (std::size_t index = 0; index < count; index++) {
		bytes[index] = static_cast<uint8>(start + index);
	}

	return bytes;
}


using Target = DfuFlashTarget<FakeFlash>;


Target MakeTarget(FakeFlash& flash) {
	return Target(flash, flash.memory.data(), RegionSize, PageSize, Key1, Key2, 7);
}


void CheckEraseAsDataArrives() {
	FakeFlash flash;
	Target target = MakeTarget(flash);
	// Begin() does not open the flash: the unlock is for the time a block is programmed.
	SIM_EXPECT_EQ(target.Begin(0), DfuStatus::OK);
	SIM_EXPECT_EQ(flash.unlockCount, 0);
	SIM_EXPECT(flash.locked);

	// The first unit only touches page 0.
	SIM_EXPECT_EQ(target.Write(0, Pattern(8, 0x10)), DfuStatus::OK);
	SIM_EXPECT_EQ(flash.unlockCount, 1);
	SIM_EXPECT(flash.locked);
	SIM_EXPECT_EQ(flash.erasedPages.size(), 1);
	SIM_EXPECT_EQ(flash.erasedPages[0], 0);

	// Data that crosses into page 1 erases page 1 at that point, and only that page.
	SIM_EXPECT_EQ(target.Write(8, Pattern(256, 0x20)), DfuStatus::OK);
	SIM_EXPECT_EQ(flash.erasedPages.size(), 2);
	SIM_EXPECT_EQ(flash.erasedPages[1], 256);
	SIM_EXPECT(std::memcmp(flash.memory.data() + 8, Pattern(256, 0x20).data(), 256) == 0);
}


void CheckShortLastUnitIsPaddedOnManifest() {
	FakeFlash flash;
	Target target = MakeTarget(flash);
	SIM_EXPECT_EQ(target.Begin(0), DfuStatus::OK);

	// 300 bytes: 37 full units, then 4 bytes that stay staged until the manifestation pads them with 0xFF.
	std::vector<uint8> image = Pattern(300, 0x00);
	SIM_EXPECT_EQ(target.Write(0, std::span<const uint8>(image).first(100)), DfuStatus::OK);
	SIM_EXPECT_EQ(target.Write(100, std::span<const uint8>(image).subspan(100)), DfuStatus::OK);
	SIM_EXPECT_EQ(flash.erasedPages.size(), 2);
	SIM_EXPECT(std::memcmp(flash.memory.data(), image.data(), 296) == 0);
	SIM_EXPECT_EQ(flash.memory[296], 0xFF);

	SIM_EXPECT_EQ(target.Manifest(), DfuStatus::OK);
	SIM_EXPECT(std::memcmp(flash.memory.data(), image.data(), 300) == 0);
	SIM_EXPECT_EQ(flash.memory[300], 0xFF);
	SIM_EXPECT_EQ(flash.memory[303], 0xFF);
	SIM_EXPECT(flash.locked);
}


void CheckWriteOutOfSequenceIsAddressError() {
	FakeFlash flash;
	Target target = MakeTarget(flash);
	SIM_EXPECT_EQ(target.Begin(0), DfuStatus::OK);
	SIM_EXPECT_EQ(target.Write(8, Pattern(8, 0)), DfuStatus::errADDRESS);

	// In sequence but longer than the region: refused before anything is erased.
	SIM_EXPECT_EQ(target.Write(0, Pattern(RegionSize + 8, 0)), DfuStatus::errADDRESS);
	SIM_EXPECT(flash.erasedPages.empty());
}


void CheckFailuresReportTheirCode() {
	FakeFlash eraseFail;
	eraseFail.eraseFails = true;
	Target eraseTarget = MakeTarget(eraseFail);
	SIM_EXPECT_EQ(eraseTarget.Begin(0), DfuStatus::OK);
	SIM_EXPECT_EQ(eraseTarget.Write(0, Pattern(8, 0)), DfuStatus::errERASE);

	// The page holds data before the erase and keeps it, so the blank check finds it.
	FakeFlash notBlank;
	notBlank.memory.fill(0x00);
	notBlank.eraseKeepsData = true;
	Target blankTarget = MakeTarget(notBlank);
	SIM_EXPECT_EQ(blankTarget.Begin(0), DfuStatus::OK);
	SIM_EXPECT_EQ(blankTarget.Write(0, Pattern(8, 0)), DfuStatus::errCHECK_ERASED);

	FakeFlash programFail;
	programFail.programFails = true;
	Target programTarget = MakeTarget(programFail);
	SIM_EXPECT_EQ(programTarget.Begin(0), DfuStatus::OK);
	SIM_EXPECT_EQ(programTarget.Write(0, Pattern(8, 0)), DfuStatus::errPROG);

	FakeFlash corrupt;
	corrupt.corruptWrites = true;
	Target verifyTarget = MakeTarget(corrupt);
	SIM_EXPECT_EQ(verifyTarget.Begin(0), DfuStatus::OK);
	SIM_EXPECT_EQ(verifyTarget.Write(0, Pattern(8, 0)), DfuStatus::errVERIFY);
}


void CheckBeginRules() {
	FakeFlash flash;
	Target target = MakeTarget(flash);
	SIM_EXPECT_EQ(target.Begin(1), DfuStatus::errTARGET);
	SIM_EXPECT_EQ(flash.unlockCount, 0);

	// An unlock that fails is reported by the block that needs the flash, and the block writes nothing.
	flash.unlockFails = true;
	SIM_EXPECT_EQ(target.Begin(0), DfuStatus::OK);
	SIM_EXPECT_EQ(target.Write(0, Pattern(8, 0)), DfuStatus::errWRITE);
	SIM_EXPECT(flash.erasedPages.empty());
	SIM_EXPECT(flash.locked);
}


// The flash is open only while a unit is programmed: after a block, after the manifestation, after a failed block and during an
// upload, which never programs anything.
void CheckFlashLockedBetweenTransfers() {
	FakeFlash flash;
	flash.memory[0] = 0x55;
	Target target = MakeTarget(flash);
	SIM_EXPECT_EQ(target.Begin(0), DfuStatus::OK);
	std::array<uint8, 4> buffer{};
	SIM_EXPECT(target.Read(0, buffer).IsOk());
	SIM_EXPECT(flash.locked);
	SIM_EXPECT_EQ(flash.unlockCount, 0);

	SIM_EXPECT_EQ(target.Write(0, Pattern(8, 0)), DfuStatus::OK);
	SIM_EXPECT(flash.locked);
	SIM_EXPECT_EQ(flash.unlockCount, 1);
	SIM_EXPECT_EQ(flash.lockCount, 1);

	SIM_EXPECT_EQ(target.Manifest(), DfuStatus::OK);
	SIM_EXPECT(flash.locked);
	SIM_EXPECT_EQ(flash.unlockCount, 2);
	SIM_EXPECT_EQ(flash.lockCount, 2);

	// A block that fails in the program step still locks the flash.
	FakeFlash failing;
	failing.programFails = true;
	Target failingTarget = MakeTarget(failing);
	SIM_EXPECT_EQ(failingTarget.Begin(0), DfuStatus::OK);
	SIM_EXPECT_EQ(failingTarget.Write(0, Pattern(8, 0)), DfuStatus::errPROG);
	SIM_EXPECT(failing.locked);
}


void CheckReadStopsAtRegionEnd() {
	FakeFlash flash;
	flash.memory[0] = 0xAA;
	flash.memory[1] = 0xBB;
	Target target = MakeTarget(flash);

	std::array<uint8, 4> buffer{};
	Result<uint32> first = target.Read(0, buffer);
	SIM_EXPECT(first.IsOk());
	SIM_EXPECT_EQ(first.Value(), 4);
	SIM_EXPECT_EQ(buffer[0], 0xAA);
	SIM_EXPECT_EQ(buffer[1], 0xBB);

	// Past the end of the region a read returns the bytes left, and nothing when none are left.
	Result<uint32> tail = target.Read(RegionSize - 2, buffer);
	SIM_EXPECT(tail.IsOk());
	SIM_EXPECT_EQ(tail.Value(), 2);
	Result<uint32> beyond = target.Read(RegionSize, buffer);
	SIM_EXPECT(beyond.IsOk());
	SIM_EXPECT_EQ(beyond.Value(), 0);
}


void CheckPollTimeoutIsTheConfiguredOne() {
	FakeFlash flash;
	Target target = MakeTarget(flash);
	SIM_EXPECT_EQ(target.GetPollTimeoutMs(DfuOperation::Write), 7);
	SIM_EXPECT_EQ(target.GetPollTimeoutMs(DfuOperation::Manifest), 7);
}


void CheckManifestWithNothingStaged() {
	FakeFlash flash;
	Target target = MakeTarget(flash);
	SIM_EXPECT_EQ(target.Begin(0), DfuStatus::OK);
	SIM_EXPECT_EQ(target.Manifest(), DfuStatus::OK);
	SIM_EXPECT(flash.erasedPages.empty());
	SIM_EXPECT_EQ(flash.lockCount, 1);
}


void CheckTargetPlugsIntoFunction() {
	// The target is the function's IDfuTarget: the function reads its layout and starts in dfuIDLE.
	FakeFlash flash;
	Target target = MakeTarget(flash);
	DfuFunction<64> function(DfuTest::ModeUsb::Function(0), target);
	SIM_EXPECT(function.GetState() == DfuState::dfuIDLE);
	SIM_EXPECT(function.GetStatus() == DfuStatus::OK);
}


}


int main() {
	CheckEraseAsDataArrives();
	CheckShortLastUnitIsPaddedOnManifest();
	CheckWriteOutOfSequenceIsAddressError();
	CheckFailuresReportTheirCode();
	CheckBeginRules();
	CheckFlashLockedBetweenTransfers();
	CheckReadStopsAtRegionEnd();
	CheckPollTimeoutIsTheConfiguredOne();
	CheckManifestWithNothingStaged();
	CheckTargetPlugsIntoFunction();
	return SimTest::Finish("TestDfuTarget");
}
