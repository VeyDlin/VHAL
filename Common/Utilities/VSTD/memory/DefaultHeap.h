#pragma once

#include "IAllocator.h"

namespace VSTD {

namespace detail {
    inline IAllocator*& DefaultHeapStorage() {
        static IAllocator* instance = nullptr;
        return instance;
    }
}

inline void SetDefaultHeap(IAllocator* heap) {
    detail::DefaultHeapStorage() = heap;
}

inline IAllocator* GetDefaultHeap() {
    return detail::DefaultHeapStorage();
}

}
