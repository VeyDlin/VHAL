#pragma once
#include <Drivers/Memory/BlockDevice/IBlockDevice.h>
#include <span>


namespace DemoUSBStorage {
	// The RAM disk seen through a counter. Each Read and Write that succeeds adds its blocks to a total, and the status line prints the
	// totals. The mass storage function calls the medium only from its Process(), so the counters change in the main loop and are read
	// there too. The other calls pass through unchanged.
	class CountingBlockDevice : public IBlockDevice {
	private:
		IBlockDevice& device;
		uint32 blocksRead = 0;
		uint32 blocksWritten = 0;


	public:
		explicit CountingBlockDevice(IBlockDevice& target)
			: device(target) {
		}


		uint32 GetBlocksRead() const {
			return blocksRead;
		}


		uint32 GetBlocksWritten() const {
			return blocksWritten;
		}


		uint32 GetBlockSize() const override {
			return device.GetBlockSize();
		}


		uint32 GetBlockCount() const override {
			return device.GetBlockCount();
		}


		ResultStatus Read(uint32 firstBlock, std::span<uint8> data) override {
			ResultStatus status = device.Read(firstBlock, data);
			if (status == ResultStatus::ok) {
				blocksRead += static_cast<uint32>(data.size() / device.GetBlockSize());
			}

			return status;
		}


		ResultStatus Write(uint32 firstBlock, std::span<const uint8> data) override {
			ResultStatus status = device.Write(firstBlock, data);
			if (status == ResultStatus::ok) {
				blocksWritten += static_cast<uint32>(data.size() / device.GetBlockSize());
			}

			return status;
		}


		ResultStatus Sync() override {
			return device.Sync();
		}


		ResultStatus IsReadOnly() override {
			return device.IsReadOnly();
		}


		ResultStatus IsPresent() override {
			return device.IsPresent();
		}
	};
}
