#pragma once
#include <DataTypes.h>
#include <Status.h>
#include <cstring>


template <size_t ElementsCount, typename Type = uint8>
class StaticFifo {
private:
	uint8 heap[sizeof(Type) * ElementsCount];
	size_t filled = 0;
	bool isReady = true;

public:
	Status::statusType Clear() {
		if(isReady == false) {
			return Status::busy;
		}
		isReady = false;
		filled = 0;
		return Status::ok;
	}


	inline bool IsReady() const {
		return isReady;
	}


	inline size_t GetCount() const {
		return filled;
	}


	Status::statusType Push(Type& data) {
		if(isReady == false) {
			return Status::busy;
		}
		isReady = false;

		if (filled >= ElementsCount) {
			isReady = true;
			return Status::filled;
		}

		std::memcpy(&heap[filled * sizeof(Type)], &data, sizeof(Type));
		filled++;

		isReady = true;
		return Status::ok;
	}


	Status::info<Type> Pop() {
		Type data;

		if(isReady == false) {
			return {Status::busy};
		}
		isReady = false;

		if (filled == 0) {
			isReady = true;
			return {Status::empty};
		}

		std::memcpy(&data, &heap[0], sizeof(Type));
		ShiftHeap();

		isReady = true;
		return {Status::ok, data};
	}

private:
	void ShiftHeap() {
		size_t endPointer = filled * sizeof(Type);
		for (size_t i = 0; i < endPointer - sizeof(Type); i++) {
			heap[i] = heap[sizeof(Type) + i];
		}

		filled--;
	}
};

