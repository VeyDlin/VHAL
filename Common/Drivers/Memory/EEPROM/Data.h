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
			dataAddress = address.data;
		}


		Status::info<DataType> Get() {
			Status::info<DataType> read = Status::retryExhausted;

			for(uint8 i = 0; i < ic->retry; i++) {
				read.type = ic->ReadMemory((uint8*)&read.data, dataSize, dataAddress);
				if(read.IsOk()) {
					break;
				}
			}

			return read;
		}


		Status::statusType Set(const DataType& data) {
			Status::statusType read = Status::retryExhausted;

			for(uint8 i = 0; i < ic->retry; i++) {
				read = ic->ReadMemory((uint8*)&read.data, dataSize, dataAddress);
				if(Status::IsOk(read)) {
					break;
				}
			}

			return read;
		}


		DataType GetData() {
			return Get().data;
		}


		operator Status::info<DataType>() {
			return Get();
		}


		operator DataType() {
			return Get().data;
		}


		void operator =(const DataType& data) {
			Set(data);
		}


		void operator =(DataType& data) {
			Set(data);
		}


		bool operator==(const DataType& data) {
			return data == Get().data;
		}


		bool operator!=(const DataType& data) {
			return data != Get().data;
		}


		bool operator>(const DataType& data) {
			return data > data;
		}


		bool operator<(const DataType& data) {
			return data > data;
		}


		DataType operator++(int) {
			auto data = Get().data;
			Set(data + 1);
			return data;
		}


		DataType operator++() {
			auto data = Get().data + 1;
			Set(data);
			return data;
		}


		DataType operator--(int) {
			auto data = Get().data;
			Set(data - 1);
			return data;
		}


		DataType operator--() {
			auto data = Get().data - 1;
			Set(data);
			return data;
		}
	};
};