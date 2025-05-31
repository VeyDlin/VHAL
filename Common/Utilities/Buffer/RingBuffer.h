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
        if (size < BufferSize) {
            size++;
        }
        return static_cast<uint16>(writeIndex);
    }


    uint16 ReadIndex() {
        uint16 out = readIndex;
        readIndex = (readIndex + 1) % BufferSize;
        if (size > 0) {
            size--;
        }
        return out;
    }


public:
    std::function<void(bool isLook)> onLook = nullptr;


    RingBuffer() : readIndex(0), size(0) {}


    Status::statusType Push(const ElementType inElement) {
		System::CriticalSection(true);
        if (IsFull()) {
			System::CriticalSection(false);
            return Status::filled;
        }

        buffer[WriteIndex()] = inElement;
        System::CriticalSection(false);

        return Status::ok;
    }


    Status::statusType Push(const ElementType* const inElement) {
        if (!inElement) {
            return Status::error; 
        }

		System::CriticalSection(true);
        if (IsFull()) {
			System::CriticalSection(false);
            return Status::filled;
        }

        buffer[WriteIndex()] = *inElement;
        System::CriticalSection(false);

        return Status::ok;
    }


    Status::info<ElementType> Pop() {
        Status::info<ElementType> out;

		System::CriticalSection(true);
        if (IsEmpty()) {
			System::CriticalSection(false);
            out.type = Status::empty;
            return out;
        }

        out.data = buffer[ReadIndex()];
        System::CriticalSection(false);

        out.type = Status::ok;
        return out;
    }


    bool IsFull() const {
        return size == BufferSize;
    }


    bool IsEmpty() const {
        return size == 0;
    }


    void Clear() {
        System::CriticalSection(true);
        readIndex = 0;
        size = 0;
        System::CriticalSection(false);
    }


    uint16 Size() const {
        return size;
    }


    constexpr uint16 MaxSize() const {
        return BufferSize;
    }


    ElementType operator[](uint16 inIndex) const {
        SystemAssert(inIndex < size);

        System::CriticalSection(true);
        uint32 index = static_cast<uint32>(readIndex) + inIndex;
        if (index >= BufferSize) {
            index -= BufferSize;
        }
        ElementType data = buffer[static_cast<uint16>(index)];
        System::CriticalSection(false);

        return data;
    }
};