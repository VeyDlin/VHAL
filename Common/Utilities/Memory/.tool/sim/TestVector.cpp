#include <cstdint>
#include <limits>
#include <type_traits>
#include <utility>
#include <Utilities/Memory/Heap.h>
#include <Utilities/Memory/PoolAllocator.h>
#include <Utilities/Memory/Vector.h>
#include "SimAllocator.h"
#include "SimCheck.h"

namespace {

// A Vector grows and frees its buffer through the allocator it was given, and no other.
void VectorUsesOnlyItsAllocator() {
    static uint8_t buffer[2048];
    Memory::Heap<> heap(buffer, sizeof(buffer));
    Memory::AllocatorOf<Memory::Heap<>> adapter(heap);
    SimRecordingAllocator recording(adapter);

    {
        Memory::Vector<int> v(&recording);
        for (int i = 0; i < 40; i++) {
            SIM_CHECK(v.PushBack(i));
        }
        SIM_CHECK(v.Size() == 40);
        SIM_CHECK(v[39] == 39);
        SIM_CHECK(recording.Allocations() == 0);
        SIM_CHECK(recording.Reallocations() >= 1);
    }

    SIM_CHECK(recording.Deallocations() == 1);
    SIM_CHECK(heap.UsedBytes() == 0);
}

// A Vector with no allocator cannot make a buffer. Every growing operation refuses and leaves the vector empty.
void VectorWithoutAllocatorRefuses() {
    Memory::Vector<int> v;
    SIM_CHECK(!v.PushBack(1));
    SIM_CHECK(v.EmplaceBack(2) == nullptr);
    SIM_CHECK(!v.Reserve(4));
    SIM_CHECK(!v.Resize(3));
    SIM_CHECK(!v.Insert(0, 5));
    SIM_CHECK(v.Size() == 0);
    SIM_CHECK(v.Capacity() == 0);

    Memory::Vector<int> copy(v);
    SIM_CHECK(copy.Empty());
    SIM_CHECK(copy.Assign(v));

    Memory::Vector<int> assigned;
    assigned = v;
    SIM_CHECK(assigned.Empty());
}

// Front and Back of a container that can be empty return a pointer, which is null when the container is empty.
void VectorAccessorsOnEmptyReturnNull() {
    static uint8_t buffer[256];
    Memory::Heap<> heap(buffer, sizeof(buffer));
    Memory::AllocatorOf<Memory::Heap<>> adapter(heap);

    Memory::Vector<int> empty(&adapter);
    SIM_CHECK(empty.Front() == nullptr);
    SIM_CHECK(empty.Back() == nullptr);

    const Memory::Vector<int>& constEmpty = empty;
    SIM_CHECK(constEmpty.Front() == nullptr);
    SIM_CHECK(constEmpty.Back() == nullptr);

    Memory::Vector<int> filled(&adapter);
    SIM_CHECK(filled.PushBack(1));
    SIM_CHECK(filled.PushBack(2));
    SIM_CHECK(*filled.Front() == 1);
    SIM_CHECK(*filled.Back() == 2);
}

// A capacity whose byte count does not fit in size_t is refused. The vector keeps its elements and capacity.
void VectorRefusesCapacityThatOverflows() {
    static uint8_t buffer[2048];
    Memory::Heap<> heap(buffer, sizeof(buffer));
    Memory::AllocatorOf<Memory::Heap<>> adapter(heap);
    Memory::Vector<int> v(&adapter);
    SIM_CHECK(v.PushBack(5));

    const std::size_t maxSize = std::numeric_limits<std::size_t>::max();
    SIM_CHECK(!v.Reserve(maxSize / 4));
    SIM_CHECK(!v.Resize(maxSize / 2));
    SIM_CHECK(v.Capacity() == 4);
    SIM_CHECK(v.Size() == 1);
    SIM_CHECK(v[0] == 5);
    SIM_CHECK(heap.UsedBytes() == 16);
}

// EmplaceBack and PushBack take an element of the vector itself. The push may grow the buffer, and the value must
// still be the one the element held before the growth.
void PushFromOwnElementAcrossGrowth() {
    static uint8_t buffer[2048];
    Memory::Heap<> heap(buffer, sizeof(buffer));
    Memory::AllocatorOf<Memory::Heap<>> adapter(heap);
    Memory::Vector<int> v(&adapter);
    for (int i = 1; i <= 4; i++) {
        SIM_CHECK(v.PushBack(i));
    }

    int* added = v.EmplaceBack(v[0]);
    SIM_CHECK(added != nullptr);
    SIM_CHECK(v.Size() == 5);
    SIM_CHECK(*added == 1);

    for (int i = 0; i < 3; i++) {
        SIM_CHECK(v.PushBack(10 + i));
    }
    SIM_CHECK(v.Size() == 8);
    SIM_CHECK(v.PushBack(std::move(v[1])));
    SIM_CHECK(v.Size() == 9);
    SIM_CHECK(v[8] == 2);
}

// The default allocator type is IAllocator, and a heap is not an IAllocator: a Vector over a heap type calls the heap
// directly, so the heap has no virtual table.
void VectorDefaultsToIAllocator() {
    static_assert(std::is_same_v<Memory::Vector<int>, Memory::Vector<int, Memory::IAllocator>>);
    static_assert(!std::is_base_of_v<Memory::IAllocator, Memory::Heap<>>);
    static_assert(!std::is_polymorphic_v<Memory::Heap<>>);
}

// A Vector over a heap type grows, copies and frees its buffer in that heap.
void VectorOverHeapTypeUsesTheHeap() {
    static uint8_t buffer[2048];
    Memory::Heap<> heap(buffer, sizeof(buffer));

    {
        Memory::Vector<int, Memory::Heap<>> v(&heap);
        for (int i = 0; i < 40; i++) {
            SIM_CHECK(v.PushBack(i));
        }
        SIM_CHECK(v.Size() == 40);
        SIM_CHECK(v[39] == 39);
        SIM_CHECK(heap.UsedBytes() > 0);

        Memory::Vector<int, Memory::Heap<>> copy(v);
        SIM_CHECK(copy.Size() == 40);
        SIM_CHECK(copy[5] == 5);

        v.Erase(0);
        SIM_CHECK(v.Size() == 39);
        SIM_CHECK(v[0] == 1);
    }

    SIM_CHECK(heap.UsedBytes() == 0);
}

// A pool is an IAllocator, so it serves the default form, and it also serves the heap-type form.
void VectorOverPoolAllocatorType() {
    Memory::PoolAllocator<32, 2> pool;
    Memory::Vector<int, Memory::PoolAllocator<32, 2>> v(&pool);
    SIM_CHECK(v.PushBack(3));
    SIM_CHECK(v.Size() == 1);
    SIM_CHECK(v[0] == 3);
}

}

int main() {
    VectorUsesOnlyItsAllocator();
    VectorWithoutAllocatorRefuses();
    VectorAccessorsOnEmptyReturnNull();
    VectorRefusesCapacityThatOverflows();
    PushFromOwnElementAcrossGrowth();
    VectorDefaultsToIAllocator();
    VectorOverHeapTypeUsesTheHeap();
    VectorOverPoolAllocatorType();
    return VstdSim::Finish("TestVector");
}
