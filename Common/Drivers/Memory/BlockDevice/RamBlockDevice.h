#pragma once
#include <Drivers/Memory/BlockDevice/IBlockDevice.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <span>


// A block device in RAM: BlockCount blocks of BlockSize bytes, held in the object. The image constructor copies a volume into it,
// such as the one Fat12Image builds, so the disk mounts with no formatting. Set the write protection and the presence with
// SetReadOnly() and SetPresent(), for example to model an ejected medium.
//
// The storage is a member array, so the device is as large as its capacity in the object that holds it. Each call is a copy of
// the requested blocks. Thread context only, like every IBlockDevice.
template<uint32 BlockSize, uint32 BlockCount>
class RamBlockDevice : public IBlockDevice {
	static_assert(BlockSize > 0, "a block holds at least one byte");
	static_assert(BlockCount > 0, "a device holds at least one block");


public:
	static constexpr std::size_t Capacity = static_cast<std::size_t>(BlockSize) * BlockCount;


private:
	std::array<uint8, Capacity> storage{};
	bool readOnly = false;
	bool present = true;


public:
	RamBlockDevice() = default;


	// Copies the image into the start of the device. A shorter image leaves the rest zero, and a longer one is cut to the capacity.
	explicit RamBlockDevice(std::span<const uint8> image) {
		std::size_t count = std::min(image.size(), Capacity);
		std::copy_n(image.begin(), count, storage.begin());
	}


	uint32 GetBlockSize() const override {
		return BlockSize;
	}


	uint32 GetBlockCount() const override {
		return BlockCount;
	}


	ResultStatus Read(uint32 firstBlock, std::span<uint8> data) override {
		ResultStatus status = CheckRange(BlockSize, BlockCount, firstBlock, data.size());
		if (status != ResultStatus::ok) {
			return status;
		}

		if (!present) {
			return ResultStatus::notExist;
		}

		std::size_t offset = static_cast<std::size_t>(firstBlock) * BlockSize;
		std::copy_n(storage.begin() + offset, data.size(), data.begin());
		return ResultStatus::ok;
	}


	ResultStatus Write(uint32 firstBlock, std::span<const uint8> data) override {
		ResultStatus status = CheckRange(BlockSize, BlockCount, firstBlock, data.size());
		if (status != ResultStatus::ok) {
			return status;
		}

		if (!present) {
			return ResultStatus::notExist;
		}

		if (readOnly) {
			return ResultStatus::readOnly;
		}

		std::size_t offset = static_cast<std::size_t>(firstBlock) * BlockSize;
		std::copy_n(data.begin(), data.size(), storage.begin() + offset);
		return ResultStatus::ok;
	}


	ResultStatus Sync() override {
		return present ? ResultStatus::ok : ResultStatus::notExist;
	}


	ResultStatus IsReadOnly() override {
		return readOnly ? ResultStatus::readOnly : ResultStatus::ok;
	}


	ResultStatus IsPresent() override {
		return present ? ResultStatus::ok : ResultStatus::notExist;
	}


	// Write protection of the medium. A write to a protected device returns readOnly.
	void SetReadOnly(bool value) {
		readOnly = value;
	}


	// False models an ejected medium: IsPresent() returns notExist and every access returns notExist.
	void SetPresent(bool value) {
		present = value;
	}


	// The whole storage, for a test or an application that reads the medium directly.
	std::span<uint8> Storage() {
		return storage;
	}
};
