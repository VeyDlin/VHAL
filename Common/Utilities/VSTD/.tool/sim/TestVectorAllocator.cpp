#include <cstdint>
#include <limits>
#include <utility>
#include <Utilities/VSTD/VSTD.h>
#include "SimAllocator.h"
#include "SimCheck.h"

namespace {

// A Vector keeps the allocator that holds its block. Growth and the final free must not move to the new default heap.
void VectorKeepsTheAllocatorThatHoldsItsBlock() {
    static uint8_t bufferA[2048];
    static uint8_t bufferB[2048];
    VSTD::Heap<> heapA(bufferA, sizeof(bufferA));
    VSTD::Heap<> heapB(bufferB, sizeof(bufferB));
    SimRecordingAllocator recordA(heapA);
    SimRecordingAllocator recordB(heapB);

    VSTD::SetDefaultHeap(&recordA);
    {
        VSTD::Vector<int> v;
        SIM_CHECK(v.PushBack(1));

        VSTD::SetDefaultHeap(&recordB);
        for (int i = 0; i < 40; i++) {
            SIM_CHECK(v.PushBack(i));
        }
        SIM_CHECK(v.Size() == 41);
        SIM_CHECK(v[0] == 1);
        SIM_CHECK(v[40] == 39);
    }

    SIM_CHECK(recordA.Deallocations() >= 1);
    SIM_CHECK(recordB.Allocations() == 0);
    SIM_CHECK(recordB.Reallocations() == 0);
    SIM_CHECK(recordB.Deallocations() == 0);
    VSTD::SetDefaultHeap(nullptr);
}

// A capacity whose byte count does not fit in size_t is refused. The vector keeps its elements and capacity.
void VectorRefusesCapacityThatOverflows() {
    static uint8_t buffer[2048];
    VSTD::Heap<> heap(buffer, sizeof(buffer));
    VSTD::Vector<int> v(&heap);
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
    VSTD::Heap<> heap(buffer, sizeof(buffer));
    VSTD::Vector<int> v(&heap);
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

}

int main() {
    VectorKeepsTheAllocatorThatHoldsItsBlock();
    VectorRefusesCapacityThatOverflows();
    PushFromOwnElementAcrossGrowth();
    return VstdSim::Finish("TestVectorAllocator");
}
