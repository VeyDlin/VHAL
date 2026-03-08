#pragma once
#include <VHAL.h>
#include <cstring>


template <size_t ElementsCount, typename Type = uint8>
class StaticFifo {
private:
    uint8 heap[sizeof(Type) * ElementsCount];
    size_t filled = 0;
    bool isReady = true;

public:
    ResultStatus Clear() {
        System::CriticalSection(true);
        if (!isReady) {
            System::CriticalSection(false);
            return ResultStatus::busy;
        }

        isReady = false;
        filled = 0;
        isReady = true;

        System::CriticalSection(false);
        return ResultStatus::ok;
    }


    inline bool IsReady() const {
        return isReady;
    }


    inline size_t GetCount() const {
        System::CriticalSection(true); 
        size_t count = filled;
        System::CriticalSection(false); 
        return count;
    }


    ResultStatus Push(const Type& data) {
        System::CriticalSection(true); 
        if (!isReady) {
            System::CriticalSection(false);
            return ResultStatus::busy;
        }

        isReady = false;

        if (filled >= ElementsCount) {
            isReady = true;
            System::CriticalSection(false);
            return ResultStatus::filled;
        }

        std::memcpy(&heap[filled * sizeof(Type)], &data, sizeof(Type));
        filled++;

        isReady = true;
        System::CriticalSection(false); 
        return ResultStatus::ok;
    }


    Result<Type> Pop() {
        System::CriticalSection(true);
        if (!isReady) {
            System::CriticalSection(false);
            return ResultStatus::busy;
        }

        isReady = false;

        if (filled == 0) {
            isReady = true;
            System::CriticalSection(false);
            return ResultStatus::empty;
        }

        Type data;
        std::memcpy(&data, &heap[0], sizeof(Type));

        ShiftHeap();

        isReady = true;
        System::CriticalSection(false);
        return data;
    }


private:
    void ShiftHeap() {
        if (filled <= 1) {
            filled = 0;
            return;
        }

        size_t remainingBytes = (filled - 1) * sizeof(Type);
        std::memmove(heap, &heap[sizeof(Type)], remainingBytes);

        filled--;
    }
};