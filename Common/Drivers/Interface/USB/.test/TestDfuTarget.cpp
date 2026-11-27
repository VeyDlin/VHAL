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
	bool readFails = false;
	// Program and read calls are counted from 0. A call at or past failProgramFrom (or failReadFrom) fails, so a test can fail one
	// unit of a block, or the verify read of a unit, and not the calls before it.
	uint32 programCalls = 0;
	uint32 failProgramFrom = 0xFFFFFFFF;
	uint32 readCalls = 0;
	uint32 failReadFrom = 0xFFFFFFFF;


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
		uint32 call = readCalls;
		readCalls++;
		if (readFails || call >= failReadFrom) {
			return Result<uint8>(Err<ResultStatus>(ResultStatus::readError));
		}

		return Result<uint8>(Ok<uint8>(*address));
	}


	ResultStatus WriteData(uint32* address, const void* data, std::size_t size) {
		uint32 call = programCalls;
		programCalls++;
		if (programFails || call >= failProgramFrom) {
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


// A region must be whole pages and whole units, and the page size must not be 0. Each of these is refused by Begin() before any erase.
void CheckRegionMustBeWholePages() {
	FakeFlash flash;

	// Not a whole number of pages: the last page would run to 1024, past the 1000-byte region.
	Target pageTail(flash, flash.memory.data(), 1000, PageSize, Key1, Key2, 7);
	SIM_EXPECT_EQ(pageTail.Begin(0), DfuStatus::errTARGET);
	SIM_EXPECT(flash.erasedPages.empty());

	// Whole pages but not whole units: the last 8-byte unit would be cut by the region's end.
	Target unitTail(flash, flash.memory.data(), 1020, 4, Key1, Key2, 7);
	SIM_EXPECT_EQ(unitTail.Begin(0), DfuStatus::errTARGET);

	// A page size of 0 would never advance the erase.
	Target noPage(flash, flash.memory.data(), RegionSize, 0, Key1, Key2, 7);
	SIM_EXPECT_EQ(noPage.Begin(0), DfuStatus::errTARGET);

	// The region as the tests use it, whole pages and whole units, is accepted.
	Target whole = MakeTarget(flash);
	SIM_EXPECT_EQ(whole.Begin(0), DfuStatus::OK);
}


// The guard in ErasePage: a page that would end past the region is never erased. Begin() refuses this region, so the test writes
// without it. That leaves ErasePage as the only check between the write and the page past the region.
void CheckErasePastRegionIsRefused() {
	FakeFlash flash;
	Target target(flash, flash.memory.data(), 1000, PageSize, Key1, Key2, 7);
	SIM_EXPECT_EQ(target.Write(0, Pattern(1000, 0)), DfuStatus::errADDRESS);

	// Pages 0, 256 and 512 lie in the region and were erased. The page at 768 runs to 1024 and was not erased.
	SIM_EXPECT_EQ(flash.erasedPages.size(), 3);
	SIM_EXPECT_EQ(flash.erasedPages[2], 512);
	SIM_EXPECT(flash.locked);
}


// A read that fails reaches the caller: the target's own read, the blank check after an erase and the verify after a program.
void CheckReadErrorsAreReported() {
	FakeFlash flash;
	flash.readFails = true;
	Target target = MakeTarget(flash);
	std::array<uint8, 4> buffer{};
	SIM_EXPECT(!target.Read(0, buffer).IsOk());
	flash.readFails = false;

	// The first read of an erase is the blank check, so it fails with errCHECK_ERASED.
	FakeFlash blankFlash;
	blankFlash.failReadFrom = 0;
	Target blankTarget = MakeTarget(blankFlash);
	SIM_EXPECT_EQ(blankTarget.Begin(0), DfuStatus::OK);
	SIM_EXPECT_EQ(blankTarget.Write(0, Pattern(8, 0)), DfuStatus::errCHECK_ERASED);

	// The erase reads one page of bytes, so the read after them is the verify of the first unit: it fails with errVERIFY.
	FakeFlash verifyFlash;
	verifyFlash.failReadFrom = PageSize;
	Target verifyTarget = MakeTarget(verifyFlash);
	SIM_EXPECT_EQ(verifyTarget.Begin(0), DfuStatus::OK);
	SIM_EXPECT_EQ(verifyTarget.Write(0, Pattern(8, 0)), DfuStatus::errVERIFY);
	SIM_EXPECT(verifyFlash.locked);
}


// A block that fails after its first unit: the unit before the failure is in memory, the failed one is not, and the flash is locked.
void CheckProgramFailureAfterFirstUnit() {
	FakeFlash flash;
	flash.failProgramFrom = 1;
	Target target = MakeTarget(flash);
	SIM_EXPECT_EQ(target.Begin(0), DfuStatus::OK);

	std::vector<uint8> block = Pattern(16, 0x10);
	SIM_EXPECT_EQ(target.Write(0, block), DfuStatus::errPROG);
	SIM_EXPECT(std::memcmp(flash.memory.data(), block.data(), 8) == 0);
	SIM_EXPECT_EQ(flash.memory[8], 0xFF);
	SIM_EXPECT(flash.locked);
}


// The padded last unit of Manifest(): its program, its verify and its page erase each report their own code, and the flash is locked
// after each of them.
void CheckManifestFailures() {
	// The program of the padded unit fails.
	FakeFlash programFlash;
	Target programTarget = MakeTarget(programFlash);
	SIM_EXPECT_EQ(programTarget.Begin(0), DfuStatus::OK);
	SIM_EXPECT_EQ(programTarget.Write(0, Pattern(4, 0x10)), DfuStatus::OK);
	programFlash.programFails = true;
	SIM_EXPECT_EQ(programTarget.Manifest(), DfuStatus::errPROG);
	SIM_EXPECT_EQ(programFlash.memory[0], 0xFF);
	SIM_EXPECT(programFlash.locked);

	// The padded unit is programmed but does not read back: errVERIFY.
	FakeFlash verifyFlash;
	verifyFlash.corruptWrites = true;
	Target verifyTarget = MakeTarget(verifyFlash);
	SIM_EXPECT_EQ(verifyTarget.Begin(0), DfuStatus::OK);
	SIM_EXPECT_EQ(verifyTarget.Write(0, Pattern(4, 0x10)), DfuStatus::OK);
	SIM_EXPECT_EQ(verifyTarget.Manifest(), DfuStatus::errVERIFY);
	SIM_EXPECT(verifyFlash.locked);

	// The padded unit sits in a page the device has not erased yet, and that erase fails. The 256 bytes before it erased page 0 and
	// the 4 staged bytes are at 256, so Manifest() erases page 256 first.
	FakeFlash eraseFlash;
	Target eraseTarget = MakeTarget(eraseFlash);
	SIM_EXPECT_EQ(eraseTarget.Begin(0), DfuStatus::OK);
	SIM_EXPECT_EQ(eraseTarget.Write(0, Pattern(256, 0)), DfuStatus::OK);
	SIM_EXPECT_EQ(eraseTarget.Write(256, Pattern(4, 0)), DfuStatus::OK);
	eraseFlash.eraseFails = true;
	SIM_EXPECT_EQ(eraseTarget.Manifest(), DfuStatus::errERASE);
	SIM_EXPECT(eraseFlash.locked);
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
	CheckRegionMustBeWholePages();
	CheckErasePastRegionIsRefused();
	CheckReadErrorsAreReported();
	CheckProgramFailureAfterFirstUnit();
	CheckManifestFailures();
	CheckTargetPlugsIntoFunction();
	return SimTest::Finish("TestDfuTarget");
}
