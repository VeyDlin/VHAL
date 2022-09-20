#pragma once
#include <DataTypes.h>
#include <string.h>


template <size_t heapSize, typename dataSizeType = uint8>
class DynamicFifo {
private:
	uint8 heap[heapSize];
	size_t endPointer = 0;
	const uint8 byteForDataSize = sizeof(dataSizeType);
	bool isReady = true;

public:
	bool Clear() {
		if(isReady == false) {
			return false;
		}
		isReady = false;
		endPointer = 0;
		return true;
	}


	inline bool IsReady() const {
		return isReady;
	}


	inline size_t GetCount() const {
		return endPointer;
	}


	template <typename dataType>
	bool Push(dataType &data, dataSizeType dataSize) {
		if(isReady == false) {
			return false;
		}
		isReady = false;

		if(heapSize < endPointer + dataSize + byteForDataSize) {
			isReady = true;
			return false;
		}

		memcpy(&heap[endPointer], &dataSize, byteForDataSize);
		endPointer += byteForDataSize;

		memcpy(&heap[endPointer], &data, dataSize);
		endPointer += dataSize;

		isReady = true;
		return true;
	}


	template <typename dataType>
	size_t Pop(dataType& data) {
		if(isReady == false) {
			return false;
		}
		isReady = false;

		if (endPointer == 0) {
			isReady = true;
			return 0;
		}

		auto dataSize = *(dataSizeType *)&heap[0];
		memcpy(&data, &heap[byteForDataSize], dataSize);

		ShiftHeap(byteForDataSize + dataSize);

		isReady = true;
		return dataSize;
	}



private:
	void ShiftHeap(size_t value) {
		if (endPointer - value <= 0) {
			endPointer = 0;
			return;
		}

		for (size_t i = 0; i < endPointer - value; i++) {
			heap[i] = heap[value + i];
		}

		endPointer -= value;
	}
};
