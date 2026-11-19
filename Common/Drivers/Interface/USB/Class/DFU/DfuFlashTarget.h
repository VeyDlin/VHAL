#pragma once
#include <array>
#include <cstddef>
#include <span>
#include <Adapter/Utilities/USBTypes.h>
#include <Drivers/Interface/USB/Class/DFU/IDfuTarget.h>
#include <Drivers/Interface/USB/Class/DFU/USBDfuProtocol.h>
#include <Utilities/Result/Result.h>


// Example target over the generic FLASH adapter (Periphery/Adapter/FLASHAdapter.h): one memory region of the flash, programmed
// in units of WriteUnit bytes. Pages are erased as the data arrives, not all at once, so an image larger than the free pages can
// be written and a failed erase is reported at the page that failed. Every unit is read back after it is programmed (errVERIFY).
//
// The download is a stream. A unit that the last block did not fill is kept until the next block completes it, and Manifest()
// pads the last unit with 0xFF and programs it. Write() and Manifest() unlock the flash for the time they program it and lock it
// again, so an upload, an abort or an error never leaves the flash open. Begin() and Read() do not touch the lock.
//
// The region size must be a multiple of WriteUnit and of the page size. Otherwise the last unit does not fit and the transfer fails
// with errADDRESS.
//
// The adapter is a template argument and only its methods are used: Unlock(key1, key2), Lock(), PageErase(uint8*), WriteData(uint32*,
// const void*, size_t), Read(uint8*) returning a Result of the byte. The FLASHAdapter of the family satisfies this; the host tests
// use a fake with the same methods. Begin() and Read() run in interrupt context, so the reads must not wait.
template<typename FlashType>
class DfuFlashTarget : public IDfuTarget {
public:
	static constexpr std::size_t WriteUnit = 8;


private:
	FlashType& flash;
	uint8* regionStart;
	uint32 regionSize;
	uint32 pageSize;
	uint32 keyOne;
	uint32 keyTwo;
	uint32 pollTimeoutMs;
	uint32 erasedTo = 0;
	uint32 writtenTo = 0;
	std::array<uint8, WriteUnit> unit{};
	std::size_t unitCount = 0;


public:
	// region is the first byte of the region in memory, size its length and page the erase unit; size is a multiple of page.
	// keyOne and keyTwo unlock the flash. pollTimeoutMs is reported for both writes and manifestation.
	DfuFlashTarget(
		FlashType& flashAdapter,
		uint8* region,
		uint32 size,
		uint32 page,
		uint32 key1,
		uint32 key2,
		uint32 pollMs)
		: flash(flashAdapter),
		  regionStart(region),
		  regionSize(size),
		  pageSize(page),
		  keyOne(key1),
		  keyTwo(key2),
		  pollTimeoutMs(pollMs) {
	}


	DfuStatus Begin(uint8 alternate) override {
		if (alternate != 0) {
			return DfuStatus::errTARGET;
		}

		erasedTo = 0;
		writtenTo = 0;
		unitCount = 0;
		return DfuStatus::OK;
	}


	DfuStatus Write(uint32 offset, std::span<const uint8> data) override {
		if (offset != writtenTo + unitCount || offset + data.size() > regionSize) {
			return DfuStatus::errADDRESS;
		}

		if (flash.Unlock(keyOne, keyTwo) != ResultStatus::ok) {
			return DfuStatus::errWRITE;
		}

		DfuStatus result = WriteBytes(data);
		flash.Lock();
		return result;
	}


	Result<uint32> Read(uint32 offset, std::span<uint8> data) override {
		uint32 count = 0;
		while (count < data.size() && offset + count < regionSize) {
			Result<uint8> value = flash.Read(regionStart + offset + count);
			if (!value.IsOk()) {
				return Result<uint32>(Err<ResultStatus>(value.Error()));
			}

			data[count] = value.Value();
			count++;
		}

		return Result<uint32>(Ok<uint32>(count));
	}


	DfuStatus Manifest() override {
		if (flash.Unlock(keyOne, keyTwo) != ResultStatus::ok) {
			return DfuStatus::errWRITE;
		}

		DfuStatus result = DfuStatus::OK;
		if (unitCount != 0) {
			for (std::size_t index = unitCount; index < WriteUnit; index++) {
				unit[index] = 0xFF;
			}

			result = WriteUnitAt(writtenTo, unit);
			writtenTo += WriteUnit;
			unitCount = 0;
		}

		flash.Lock();
		return result;
	}


	uint32 GetPollTimeoutMs(DfuOperation operation) const override {
		static_cast<void>(operation);
		return pollTimeoutMs;
	}


private:
	// Takes the bytes of a block into the units, programming each full unit. The caller holds the flash unlocked.
	DfuStatus WriteBytes(std::span<const uint8> data) {
		for (uint8 value : data) {
			unit[unitCount] = value;
			unitCount++;
			if (unitCount == WriteUnit) {
				DfuStatus result = WriteUnitAt(writtenTo, unit);
				if (result != DfuStatus::OK) {
					return result;
				}

				writtenTo += WriteUnit;
				unitCount = 0;
			}
		}

		return DfuStatus::OK;
	}


	// Programs one unit at a region offset. The pages up to the unit are erased first, one at a time, and each erased page is
	// checked blank before the unit goes in.
	DfuStatus WriteUnitAt(uint32 address, const std::array<uint8, WriteUnit>& bytes) {
		if (address + WriteUnit > regionSize) {
			return DfuStatus::errADDRESS;
		}

		while (erasedTo <= address) {
			DfuStatus erased = ErasePage(erasedTo);
			if (erased != DfuStatus::OK) {
				return erased;
			}

			erasedTo += pageSize;
		}

		if (flash.WriteData(reinterpret_cast<uint32*>(regionStart + address), bytes.data(), WriteUnit) != ResultStatus::ok) {
			return DfuStatus::errPROG;
		}

		for (std::size_t index = 0; index < WriteUnit; index++) {
			Result<uint8> readBack = flash.Read(regionStart + address + index);
			if (!readBack.IsOk() || readBack.Value() != bytes[index]) {
				return DfuStatus::errVERIFY;
			}
		}

		return DfuStatus::OK;
	}


	// Erases the page at a region offset and checks that every byte of it reads 0xFF.
	DfuStatus ErasePage(uint32 pageOffset) {
		if (flash.PageErase(regionStart + pageOffset) != ResultStatus::ok) {
			return DfuStatus::errERASE;
		}

		for (uint32 index = 0; index < pageSize; index++) {
			Result<uint8> value = flash.Read(regionStart + pageOffset + index);
			if (!value.IsOk() || value.Value() != 0xFF) {
				return DfuStatus::errCHECK_ERASED;
			}
		}

		return DfuStatus::OK;
	}
};
