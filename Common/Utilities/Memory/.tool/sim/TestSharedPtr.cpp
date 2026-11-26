#include <cstdint>
#include <Utilities/Memory/Heap.h>
#include <Utilities/Memory/SharedPtr.h>
#include "SimCheck.h"

namespace {

// Copies share one object, and the object is freed with the last copy. Its block returns to the heap.
void CopiesShareOneObject() {
    static uint8_t buffer[512];
    Memory::Heap<> heap(buffer, sizeof(buffer));

    {
        Memory::SharedPtr<int> first = Memory::AllocateShared<int>(&heap, 7);
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

    Memory::Handle front = heap.Allocate(64);
    Memory::SharedPtr<int> shared = Memory::AllocateShared<int>(&heap, 42);
    SIM_CHECK(static_cast<bool>(shared));

    heap.Deallocate(front);
    heap.Defragment();
    SIM_CHECK(*shared == 42);
}

// An allocation the allocator refuses gives an empty SharedPtr, which tests false.
void RefusedAllocationIsEmpty() {
    static uint8_t buffer[16];
    Memory::Heap<> heap(buffer, sizeof(buffer));

    Memory::SharedPtr<int> refused = Memory::AllocateShared<int>(&heap, 1);
    SIM_CHECK(!static_cast<bool>(refused));
    SIM_CHECK(refused.UseCount() == 0);
}

}

int main() {
    CopiesShareOneObject();
    ObjectFollowsDefragment();
    RefusedAllocationIsEmpty();
    return VstdSim::Finish("TestSharedPtr");
}
