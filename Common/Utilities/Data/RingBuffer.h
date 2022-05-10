#pragma once
#include <System/System.h>
#include <limits>
#include <functional>



template<typename ElementType, uint16 BufferSize>
class RingBuffer {
	static_assert(BufferSize > 0, "RingBuffer with size 0 are forbidden");

private:
	ElementType buffer[BufferSize];
	uint16 readIndex;
	uint16 size;


private:
	uint16 WriteIndex() {
		uint32 writeIndex = readIndex + size;
		if (writeIndex >= BufferSize) {
			writeIndex -= BufferSize;
		}
		size++;
		return static_cast<uint16>(writeIndex);
	}

	uint16 ReadIndex() {
		uint16 out = readIndex;
		readIndex++;
		size--;
		if (readIndex == BufferSize) {
			readIndex = 0;
		}
		return out;
	}



public:
	std::function<void(bool isLook)> onLook = nullptr;


public:
	RingBuffer() : readIndex(0), size(0) { }





	Status::statusType Push(const ElementType inElement) {
		if (IsFull()) {
			return Status::filled;
		}

		System::CriticalSection(true);
		buffer[WriteIndex()] = inElement;
		System::CriticalSection(false);

		return Status::ok;
	}

	



	Status::statusType Push(const ElementType *const inElement) {
		if (IsFull()) {
			Status::filled;
		}

		System::CriticalSection(true);
		buffer[WriteIndex()] = *inElement;
		System::CriticalSection(false);

		return Status::ok;
	}





	Status::info<ElementType> Pop() {
		Status::info<ElementType> out;
		if (IsEmpty()) {
			out.type = Status::empty;
			return out;
		}

		System::CriticalSection(true);
		out.data = buffer[ReadIndex()];
		System::CriticalSection(false);

		out.type = Status::ok;
		return out;
	}





	bool IsFull() {
		return size == BufferSize;
	}

	bool IsEmpty() {
		return size == 0;
	}

	bool IsReady() {
		return size != 0;
	}

	void Clear() {
		size = 0;
	}

	uint16 Size() {
		return size;
	}

	uint16 MaxSize() {
		return BufferSize;
	}





	ElementType operator[](uint16 inIndex) {
		SystemAssert(inIndex < size);

		System::CriticalSection(true);
		uint32 index = (uint32)readIndex + (uint32)inIndex;
		if (index >= (uint32)BufferSize) {
			index -= (uint32)BufferSize;
		}
		ElementType data = buffer[(uint16)index];
		System::CriticalSection(false);

		return data;
	}

};









