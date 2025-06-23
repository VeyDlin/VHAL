#pragma once
#include <System/System.h>
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
        System::CriticalSection(true); 
        if (!isReady) {
            System::CriticalSection(false);
            return false;
        }
        isReady = false;
        endPointer = 0;
        isReady = true;
        System::CriticalSection(false); 
        return true;
    }

    inline bool IsReady() const {
        return isReady;
    }

    inline size_t GetCount() const {
        System::CriticalSection(true);
        size_t count = endPointer;
        System::CriticalSection(false);
        return count;
    }

    template <typename dataType>
    bool Push(const dataType& data, dataSizeType dataSize) {
        System::CriticalSection(true);
        if (!isReady) {
            System::CriticalSection(false);
            return false;
        }
        isReady = false;

        if (heapSize < endPointer + dataSize + byteForDataSize) {
            isReady = true;
            System::CriticalSection(false);
            return false;
        }

        memcpy(&heap[endPointer], &dataSize, byteForDataSize);
        endPointer += byteForDataSize;

        memcpy(&heap[endPointer], &data, dataSize);
        endPointer += dataSize;

        isReady = true;
        System::CriticalSection(false); 
        return true;
    }

    template <typename dataType>
    size_t Pop(dataType& data) {
        System::CriticalSection(true);
        if (!isReady) {
            System::CriticalSection(false);
            return 0;
        }
        isReady = false;

        if (endPointer == 0) {
            isReady = true;
            System::CriticalSection(false);
            return 0;
        }

        auto dataSize = *(dataSizeType*)&heap[0];
        memcpy(&data, &heap[byteForDataSize], dataSize);

        ShiftHeap(byteForDataSize + dataSize);

        isReady = true;
        System::CriticalSection(false);
        return dataSize;
    }


private:
    void ShiftHeap(size_t value) {
        System::CriticalSection(true); 
        if (value >= endPointer) {
            endPointer = 0;
            System::CriticalSection(false);
            return;
        }

        memmove(heap, &heap[value], endPointer - value);
        endPointer -= value;
        System::CriticalSection(false); 
    }
};
