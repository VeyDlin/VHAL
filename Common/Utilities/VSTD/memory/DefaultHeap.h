#pragma once

#include <Utilities/Memory/IAllocator.h>

namespace VSTD {

namespace detail {
    inline Memory::IAllocator*& DefaultHeapStorage() {
        static Memory::IAllocator* instance = nullptr;
        return instance;
    }
}

inline void SetDefaultHeap(Memory::IAllocator* heap) {
    detail::DefaultHeapStorage() = heap;
}

inline Memory::IAllocator* GetDefaultHeap() {
    return detail::DefaultHeapStorage();
}

}
