#include <cstdint>
#include <Utilities/Memory/Heap.h>
#include <Utilities/Memory/SharedPtr.h>
#include "SimCheck.h"

namespace {

// Copies share one object, and the object is freed with the last copy. Its block returns to the heap.
void CopiesShareOneObject() {
    static uint8_t buffer[512];
    Memory::Heap<> heap(buffer, sizeof(buffer));
    Memory::AllocatorOf<Memory::Heap<>> adapter(heap);

    {
        Memory::SharedPtr<int> first = Memory::AllocateShared<int>(&adapter, 7);
        SIM_CHECK(static_cast<bool>(first));
        SIM_CHECK(first.UseCount() == 1);

        Memory::SharedPtr<int> second = first;
        SIM_CHECK(first.UseCount() == 2);
        SIM_CHECK(second.Get() == first.Get());
        SIM_CHECK(*second == 7);

        second.Reset();
        SIM_CHECK(first.IsUnique());
        SIM_CHECK(heap.UsedBytes() > 0);
    }

    SIM_CHECK(heap.UsedBytes() == 0);
}

// The object sits in a movable block. After Defragment() moves it, the pointer taken through the SharedPtr is valid.
void ObjectFollowsDefragment() {
    static uint8_t buffer[1024];
    Memory::Heap<Memory::IncrementalDefrag<4>> heap(buffer, sizeof(buffer));
    Memory::AllocatorOf<Memory::Heap<Memory::IncrementalDefrag<4>>> adapter(heap);

    Memory::Handle front = heap.Allocate(64);
    Memory::SharedPtr<int> shared = Memory::AllocateShared<int>(&adapter, 42);
    SIM_CHECK(static_cast<bool>(shared));

    heap.Deallocate(front);
    heap.Defragment();
    SIM_CHECK(*shared == 42);
}

// A SharedPtr over a heap type allocates its control block in that heap and frees it with the last copy.
void SharedPtrOverHeapType() {
    static uint8_t buffer[512];
    Memory::Heap<> heap(buffer, sizeof(buffer));

    {
        Memory::SharedPtr<int, Memory::Heap<>> first = Memory::AllocateShared<int, Memory::Heap<>>(&heap, 9);
        SIM_CHECK(static_cast<bool>(first));
        Memory::SharedPtr<int, Memory::Heap<>> second = first;
        SIM_CHECK(first.UseCount() == 2);
        SIM_CHECK(*second == 9);
        SIM_CHECK(heap.UsedBytes() > 0);
    }

    SIM_CHECK(heap.UsedBytes() == 0);
}

// An allocation the allocator refuses gives an empty SharedPtr, which tests false.
void RefusedAllocationIsEmpty() {
    static uint8_t buffer[16];
    Memory::Heap<> heap(buffer, sizeof(buffer));
    Memory::AllocatorOf<Memory::Heap<>> adapter(heap);

    Memory::SharedPtr<int> refused = Memory::AllocateShared<int>(&adapter, 1);
    SIM_CHECK(!static_cast<bool>(refused));
    SIM_CHECK(refused.UseCount() == 0);
}

}

int main() {
    CopiesShareOneObject();
    ObjectFollowsDefragment();
    RefusedAllocationIsEmpty();
    SharedPtrOverHeapType();
    return VstdSim::Finish("TestSharedPtr");
}
