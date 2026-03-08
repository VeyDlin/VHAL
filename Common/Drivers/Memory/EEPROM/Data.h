#pragma once
#include "Eeprom.h"


namespace EEPROM {
	template<typename DataType>
	struct Data {
		Eeprom *ic;
		uint16 dataSize = sizeof(DataType);
		uint16 dataAddress;


		Data() {}


		Data(Eeprom &eeprom): ic(&eeprom) {
			auto address = ic->GetMemoryAddress(dataSize);
			dataAddress = address.Value();
		}


		Result<DataType> Get() {
			DataType data{};
			ResultStatus readStatus = ResultStatus::retryExhausted;

			for(uint8 i = 0; i < ic->retry; i++) {
				readStatus = ic->ReadMemory((uint8*)&data, dataSize, dataAddress);
				if(readStatus == ResultStatus::ok) {
					break;
				}
			}

			return Result<DataType>::Capture(readStatus, data);
		}


		ResultStatus Set(const DataType& data) {
			ResultStatus read = ResultStatus::retryExhausted;

			for(uint8 i = 0; i < ic->retry; i++) {
				read = ic->WriteMemory((uint8*)&data, dataSize, dataAddress);
				if(read == ResultStatus::ok) {
					break;
				}
			}

			return read;
		}


		DataType GetData() {
			return Get().Value();
		}


		operator Result<DataType>() {
			return Get();
		}


		operator DataType() {
			return Get().Value();
		}


		void operator =(const DataType& data) {
			Set(data);
		}


		void operator =(DataType& data) {
			Set(data);
		}


		bool operator==(const DataType& data) {
			return data == Get().Value();
		}


		bool operator!=(const DataType& data) {
			return data != Get().Value();
		}


		bool operator>(const DataType& data) {
			return data > data;
		}


		bool operator<(const DataType& data) {
			return data > data;
		}


		DataType operator++(int) {
			auto data = Get().Value();
			Set(data + 1);
			return data;
		}


		DataType operator++() {
			auto data = Get().Value() + 1;
			Set(data);
			return data;
		}


		DataType operator--(int) {
			auto data = Get().Value();
			Set(data - 1);
			return data;
		}


		DataType operator--() {
			auto data = Get().Value() - 1;
			Set(data);
			return data;
		}
	};
};