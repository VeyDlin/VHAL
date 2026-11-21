#pragma once
#include <VHAL.h>
#include <DemoProducts.h>
#include <Drivers/Interface/USB/Class/DFU/DfuFlashTarget.h>
#include <Drivers/Interface/USB/Class/DFU/IDfuTarget.h>
#include <Drivers/Interface/USB/Class/DFU/USBDfuProtocol.h>
#include <Utilities/Result/Result.h>
#include <cstddef>
#include <cstdint>
#include <span>


namespace DemoUSBDfu {
	// The region the demo writes: the top 32 KB of the G431's flash, sixteen 2 KB pages. The image of the demo must end below it,
	// which the linker checks (STM32G431XB_FLASH.ld). Both values come from CMakeLists.txt, where the demo sets them.
	constexpr uint32 RegionStart = VHAL_DFU_REGION_START;
	constexpr uint32 RegionSize = VHAL_DFU_REGION_SIZE;
	// The PC tool (usbclasscheck) and the host's view of the region use the shared values, so the CMake values must be the same.
	static_assert(RegionStart == DemoProducts::DfuRegionStart, "VHAL_DFU_REGION_START must be DemoProducts::DfuRegionStart");
	static_assert(RegionSize == DemoProducts::DfuRegionSize, "VHAL_DFU_REGION_SIZE must be DemoProducts::DfuRegionSize");
	// The G431 erases in 2 KB pages, and DfuFlashTarget erases the region one page at a time.
	constexpr uint32 RegionPageSize = 2048;
	// The RM0440 keys that unlock the flash. The FLASH adapter checks them too; DfuFlashTarget passes them to Unlock().
	constexpr uint32 FlashKey1 = 0x45670123;
	constexpr uint32 FlashKey2 = 0xCDEF89AB;
	// The poll timeout the host waits between status requests while a block is written or the image is completed, in milliseconds.
	// One block of this demo is one page (DfuTransferSize equals RegionPageSize). The write erases the page, programs it one double word
	// at a time with a read-back of each, and then checks that the page is blank. The per-operation times below are assumptions, not
	// datasheet figures, and they are to be measured on the board:
	//   - the page erase takes at most PageEraseAssumedMs. That is the FLASH adapter's own bound (PageEraseTimeoutMs, 100 ms).
	//   - one double word with its read-back takes at most ProgramUnitAssumedMs. The adapter's bound is 10 ms per double word.
	//   - the blank check and the rest of the block take at most CheckMarginMs.
	// A block that runs longer than the timeout is still answered busy by DfuFunction, so the host polls again. A short assumption
	// costs an extra poll; it does not fail the download.
	constexpr uint32 PageEraseAssumedMs = 100;
	constexpr uint32 ProgramUnitAssumedMs = 1;
	constexpr uint32 CheckMarginMs = 50;
	constexpr uint32 UnitsPerPage = RegionPageSize / static_cast<uint32>(DfuFlashTarget<AFLASH>::WriteUnit);
	constexpr uint32 PollTimeoutMs = PageEraseAssumedMs + UnitsPerPage * ProgramUnitAssumedMs + CheckMarginMs;


	// The memory target of the DFU mode function: the library's FLASH example target over the region, with the byte counts the status
	// line shows and the hand-off of a completed download to the main loop. Begin() and Read() run in interrupt context, and Read()
	// is the only writer of uploadedBytes there; Write() and Manifest() run in the main loop (DfuFunction::Process()). The count the
	// main loop reads from the interrupt is taken under a critical section.
	//
	// Where the code runs: Write() and Manifest() run from flash, in the main loop. On the single-bank G431 the CPU stalls while a page
	// is erased or programmed, and the interrupts wait with it, so the USB interrupt waits during a page erase. The host's control
	// requests are NAKed meanwhile and complete afterwards. Not measured yet: to be confirmed on the board (see the README).
	class DfuRegionTarget : public IDfuTarget {
	private:
		DfuFlashTarget<AFLASH> flashTarget;
		// Bytes the flash took from downloads, since start. Written in the main loop only.
		uint32 downloadedBytes = 0;
		// Bytes the uploads sent, since start. Written in the interrupt by Read().
		uint32 uploadedBytes = 0;
		// The end offset of the current download's last block. Begin() resets it.
		uint32 transferBytes = 0;
		// The size of the last completed download, handed to the main loop once by TakeManifestedBytes().
		uint32 manifestedBytes = 0;
		bool manifestPending = false;


	public:
		explicit DfuRegionTarget(AFLASH& flash)
			: flashTarget(flash, reinterpret_cast<uint8*>(static_cast<uintptr_t>(RegionStart)), RegionSize, RegionPageSize, FlashKey1, FlashKey2, PollTimeoutMs) {
		}


		DfuStatus Begin(uint8 alternate) override {
			transferBytes = 0;
			return flashTarget.Begin(alternate);
		}


		DfuStatus Write(uint32 offset, std::span<const uint8> data) override {
			DfuStatus result = flashTarget.Write(offset, data);
			if (result == DfuStatus::OK) {
				downloadedBytes += static_cast<uint32>(data.size());
				transferBytes = offset + static_cast<uint32>(data.size());
			}

			return result;
		}


		Result<uint32> Read(uint32 offset, std::span<uint8> data) override {
			Result<uint32> result = flashTarget.Read(offset, data);
			if (result.IsOk()) {
				uploadedBytes += result.Value();
			}

			return result;
		}


		DfuStatus Manifest() override {
			DfuStatus result = flashTarget.Manifest();
			if (result == DfuStatus::OK) {
				manifestedBytes = transferBytes;
				manifestPending = true;
			}

			return result;
		}


		uint32 GetPollTimeoutMs(DfuOperation operation) const override {
			return flashTarget.GetPollTimeoutMs(operation);
		}


		bool IsComplete() const override {
			return flashTarget.IsComplete();
		}


		// Main loop only. Returns true once after each completed download, with the number of bytes it wrote.
		bool TakeManifestedBytes(uint32& bytes) {
			if (!manifestPending) {
				return false;
			}

			manifestPending = false;
			bytes = manifestedBytes;
			return true;
		}


		// Main loop only.
		uint32 GetDownloadedBytes() const {
			return downloadedBytes;
		}


		uint32 GetUploadedBytes() const {
			System::CriticalSection(true);
			uint32 bytes = uploadedBytes;
			System::CriticalSection(false);
			return bytes;
		}
	};
}
