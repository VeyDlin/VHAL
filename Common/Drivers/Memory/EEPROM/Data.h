#pragma once
#include "Eeprom.h"


namespace EEPROM {

	template<typename DataType>
	struct Data {
		Eeprom *chipClass;
		uint16 dataSize = sizeof(DataType);
		uint16 dataAddress;

		Data() {}
		Data(Eeprom &setChipClass): chipClass(&setChipClass) {
			auto address = chipClass->GetMemoryAddress(dataSize);
			dataAddress = address.data;
		}

		Status::info<DataType> Get() {
			DataType data;
			auto status = chipClass->ReadMemory((uint8*)&data, dataSize, dataAddress);
			if(status != Status::ok) {
				return {status};
			}
			return {Status::ok, data};
		}

		DataType GetData() {
			return Get().data;
		}

		Status::statusType Set(const DataType& data) {
			return chipClass->WriteMemory((uint8*)&data, dataSize, dataAddress);
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
