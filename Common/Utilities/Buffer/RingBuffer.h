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
    uint16 uncommittedReadCount = 0;  // How many reads are uncommitted


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
        uncommittedReadCount = 0;
        System::CriticalSection(false);
    }


    uint16 Size() const {
        return size;
    }


    constexpr uint16 MaxSize() const {
        return BufferSize;
    }


    uint16 GetFreeSpace() const {
        return BufferSize - size;
    }


    Status::info<ElementType> Peek() const {
        Status::info<ElementType> out;
        if (IsEmpty()) {
            out.type = Status::empty;
            return out;
        }
        out.data = buffer[readIndex];
        out.type = Status::ok;
        return out;
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


    // Peek at multiple elements without removing them
    // Returns actual number of elements peeked (may be less than requested)
    uint16 PeekMultiple(ElementType* outBuffer, uint16 count, uint16 offset = 0) const {
        System::CriticalSection(true);
        
        // Can't peek more than we have
        if (offset >= size) {
            System::CriticalSection(false);
            return 0;
        }
        
        uint16 available = size - offset;
        uint16 toPeek = (count < available) ? count : available;
        
        for (uint16 i = 0; i < toPeek; i++) {
            uint32 index = static_cast<uint32>(readIndex) + offset + i;
            if (index >= BufferSize) {
                index -= BufferSize;
            }
            outBuffer[i] = buffer[static_cast<uint16>(index)];
        }
        
        System::CriticalSection(false);
        return toPeek;
    }


    // Mark elements as read but don't remove them yet
    Status::statusType MarkRead(uint16 count) {
        System::CriticalSection(true);
        
        if (count > size - uncommittedReadCount) {
            System::CriticalSection(false);
            return Status::error;  // Can't mark more than available
        }
        
        uncommittedReadCount += count;
        
        System::CriticalSection(false);
        return Status::ok;
    }


    // Commit all marked reads - actually remove them from buffer
    void CommitReads() {
        System::CriticalSection(true);
        
        while (uncommittedReadCount > 0) {
            readIndex = (readIndex + 1) % BufferSize;
            size--;
            uncommittedReadCount--;
        }
        
        System::CriticalSection(false);
    }


    // Rollback marked reads - cancel uncommitted reads
    void RollbackReads() {
        System::CriticalSection(true);
        uncommittedReadCount = 0;
        System::CriticalSection(false);
    }


    // Get number of uncommitted reads
    uint16 GetUncommittedCount() const {
        return uncommittedReadCount;
    }
};