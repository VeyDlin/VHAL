#pragma once
#include "Eeprom.h"
#include <type_traits>


// A record in an EEPROM. The constructor reserves the next free place for DataType in the device. A record that does not fit
// is not placed: GetAllocation() returns outOfRange, and every call on the record returns that status.
//
// Every call returns a status or a Result, so a bus error is never hidden. There is no conversion to the value and no
// assignment from it. The comparisons read the record and return Result<bool>. There is no increment or decrement: a
// read-modify-write is two bus transactions, and a power cut between them leaves the old or the new value.
namespace EEPROM {
	template<typename DataType>
	struct Data {
		static_assert(std::is_trivially_copyable_v<DataType>, "EEPROM::Data copies the bytes of DataType to the bus, so DataType must be trivially copyable");

	public:
		static constexpr uint32 dataSize = sizeof(DataType);


	private:
		Eeprom *ic = nullptr;
		uint32 dataAddress = 0;
		ResultStatus allocation = ResultStatus::noInit;


	public:
		// A record that is not placed in any device. Every call returns noInit.
		Data() = default;


		// Reserves the next free place for DataType in the device. A conversion from Eeprom is not offered, because it would
		// reserve space without a name.
		explicit Data(Eeprom &eeprom):
			ic(&eeprom)
		{
			Result<uint32> address = ic->GetMemoryAddress(dataSize);

			if (address.IsOk()) {
				dataAddress = address.Value();
				allocation = ResultStatus::ok;
			} else {
				allocation = address.Error();
			}
		}


		// The first byte of the record. An error when the record is not placed.
		Result<uint32> GetAddress() const {
			if (allocation != ResultStatus::ok) {
				return allocation;
			}

			return dataAddress;
		}


		ResultStatus GetAllocation() const {
			return allocation;
		}


		Result<DataType> Get() {
			if (allocation != ResultStatus::ok) {
				return allocation;
			}

			DataType data{};
			ResultStatus readStatus = ResultStatus::retryExhausted;

			for (uint8 i = 0; i < ic->retry; i++) {
				readStatus = ic->ReadMemory(reinterpret_cast<uint8 *>(&data), dataSize, dataAddress);
				if (readStatus == ResultStatus::ok) {
					break;
				}
			}

			return Result<DataType>::Capture(readStatus, data);
		}


		// Writes the record. A failed write is repeated up to retry times, and each repeat waits for the write cycle inside
		// WriteMemory. A timeout is not repeated, because the chip did not finish its write cycle in time.
		ResultStatus Set(const DataType &data) {
			if (allocation != ResultStatus::ok) {
				return allocation;
			}

			ResultStatus writeStatus = ResultStatus::retryExhausted;

			for (uint8 i = 0; i < ic->retry; i++) {
				writeStatus = ic->WriteMemory(reinterpret_cast<const uint8 *>(&data), dataSize, dataAddress);
				if (writeStatus == ResultStatus::ok || writeStatus == ResultStatus::timeout) {
					break;
				}
			}

			return writeStatus;
		}


		Result<bool> operator==(const DataType &other) {
			return Compare(other, [](const DataType &stored, const DataType &value) {
				return stored == value;
			});
		}


		Result<bool> operator!=(const DataType &other) {
			return Compare(other, [](const DataType &stored, const DataType &value) {
				return stored != value;
			});
		}


		Result<bool> operator<(const DataType &other) {
			return Compare(other, [](const DataType &stored, const DataType &value) {
				return stored < value;
			});
		}


		Result<bool> operator>(const DataType &other) {
			return Compare(other, [](const DataType &stored, const DataType &value) {
				return stored > value;
			});
		}


	private:
		template<typename Relation>
		Result<bool> Compare(const DataType &other, Relation relation) {
			Result<DataType> stored = Get();
			if (stored.IsErr()) {
				return stored.Error();
			}

			return relation(stored.Value(), other);
		}
	};
};
