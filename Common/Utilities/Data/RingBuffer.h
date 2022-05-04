#pragma once
#include <BSP.h>
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

	inline void OnLook(bool isLook) {
		if(onLook != nullptr) {
			onLook(isLook);
		}
	}



public:
	std::function<void(bool isLook)> onLook = nullptr;


public:
	RingBuffer() : readIndex(0), size(0) { }





	bool Push(const ElementType inElement) {
		if (IsFull()) {
			return false;
		}

		OnLook(true);
		buffer[WriteIndex()] = inElement;
		OnLook(false);

		return true;
	}





	bool Push(const ElementType *const inElement) {
		if (IsFull()) {
			return false;
		}

		OnLook(true);
		buffer[WriteIndex()] = *inElement;
		OnLook(false);

		return true;
	}





	bool Pop(ElementType &outElement) {
		if (IsEmpty()) {
			return false;
		}

		OnLook(true);
		outElement = buffer[ReadIndex()];
		OnLook(false);

		return true;
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





	ElementType& operator[](uint16 inIndex) {
		if (inIndex >= size) {
			return buffer[0];
		}
		uint32 index = (uint32)readIndex + (uint32)inIndex;
		if (index >= (uint32)BufferSize) {
			index -= (uint32)BufferSize;
		}
		return buffer[(uint16)index];
	}

};









