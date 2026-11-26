#include <array>
#include <cstdint>
#include <utility>
#include <Utilities/Memory/Heap.h>
#include <Utilities/Memory/Vector.h>
#include <Utilities/VSTD/utility/Function.h>
#include <Utilities/Memory/.tool/sim/SimAllocator.h>
#include <Utilities/Memory/.tool/sim/SimCheck.h>

namespace {

// Captures 64 bytes, more than the small buffer of Function, so the callable is stored in an allocator block.
VSTD::Function<int()> MakeBigCallable(Memory::IAllocator* alloc) {
    std::array<int, 16> values{};
    values[15] = 7;
    return VSTD::Function<int()>(alloc, [values]() { return values[15]; });
}

// A callable stored in a block uses the allocator it was given, not the default heap.
void InjectedAllocatorServesTheBlock() {
    static uint8_t bufferA[1024];
    static uint8_t bufferB[1024];
    Memory::Heap<> heapA(bufferA, sizeof(bufferA));
    Memory::Heap<> heapB(bufferB, sizeof(bufferB));
    Memory::AllocatorOf<Memory::Heap<>> adapterA(heapA);
    Memory::AllocatorOf<Memory::Heap<>> adapterB(heapB);
    SimRecordingAllocator recordA(adapterA);
    SimRecordingAllocator recordB(adapterB);
    VSTD::SetDefaultHeap(&recordB);

    {
        VSTD::Function<int()> f = MakeBigCallable(&recordA);
        SIM_CHECK(static_cast<bool>(f));
        SIM_CHECK(f() == 7);
        SIM_CHECK(recordA.Allocations() == 1);
        SIM_CHECK(recordB.Allocations() == 0);
    }

    SIM_CHECK(recordA.Deallocations() == 1);
    SIM_CHECK(recordB.Deallocations() == 0);
    VSTD::SetDefaultHeap(nullptr);
}

// The allocator is remembered when the callable is made. Changing the default heap before destruction must not
// send the free to the new default.
void BlockIsFreedToTheAllocatorThatMadeIt() {
    static uint8_t bufferA[1024];
    static uint8_t bufferB[1024];
    Memory::Heap<> heapA(bufferA, sizeof(bufferA));
    Memory::Heap<> heapB(bufferB, sizeof(bufferB));
    Memory::AllocatorOf<Memory::Heap<>> adapterA(heapA);
    Memory::AllocatorOf<Memory::Heap<>> adapterB(heapB);
    SimRecordingAllocator recordA(adapterA);
    SimRecordingAllocator recordB(adapterB);

    VSTD::SetDefaultHeap(&recordA);
    {
        std::array<int, 16> values{};
        values[15] = 9;
        VSTD::Function<int()> f([values]() { return values[15]; });
        SIM_CHECK(recordA.Allocations() == 1);

        VSTD::SetDefaultHeap(&recordB);
        SIM_CHECK(f() == 9);
    }

    SIM_CHECK(recordA.Deallocations() == 1);
    SIM_CHECK(recordB.Deallocations() == 0);
    VSTD::SetDefaultHeap(nullptr);
}

// When the allocator refuses the block, the Function is empty and the caller can see it.
void RefusedAllocationIsVisible() {
    static uint8_t tinyBuffer[64];
    Memory::Heap<> tiny(tinyBuffer, sizeof(tinyBuffer));
    Memory::AllocatorOf<Memory::Heap<>> tinyAdapter(tiny);

    VSTD::Function<int()> f = MakeBigCallable(&tinyAdapter);
    SIM_CHECK(!static_cast<bool>(f));
    SIM_CHECK(f() == 0);
}

// Calling an empty Function has a defined result: a value-initialized return value, and nothing for void.
void EmptyCallableIsDefined() {
    VSTD::Function<int()> empty;
    SIM_CHECK(!static_cast<bool>(empty));
    SIM_CHECK(empty() == 0);

    VSTD::Function<int(int)> emptyWithArgument;
    SIM_CHECK(emptyWithArgument(3) == 0);

    VSTD::Function<void()> emptyVoid;
    emptyVoid();
    SIM_CHECK(!static_cast<bool>(emptyVoid));
}

// A callable moved out of a small buffer still works, and the source is empty afterwards.
void SmallCallableMoves() {
    VSTD::Function<int()> f([]() { return 4; });
    VSTD::Function<int()> g(std::move(f));
    SIM_CHECK(g() == 4);
    SIM_CHECK(!static_cast<bool>(f));
}

// Vector growth copies the elements' bytes to a new block. Every callable must still run afterwards.
void CallablesSurviveVectorGrowth() {
    static uint8_t buffer[8192];
    Memory::Heap<> heap(buffer, sizeof(buffer));
    Memory::AllocatorOf<Memory::Heap<>> adapter(heap);
    VSTD::SetDefaultHeap(&adapter);

    {
        Memory::Vector<VSTD::Function<int()>> v(&adapter);
        for (int i = 0; i < 9; i++) {
            SIM_CHECK(v.PushBack(VSTD::Function<int()>([i]() { return i * 10; })));
        }

        std::array<int, 16> values{};
        values[15] = 77;
        SIM_CHECK(v.PushBack(VSTD::Function<int()>([values]() { return values[15]; })));

        for (int i = 0; i < 9; i++) {
            SIM_CHECK(v[static_cast<std::size_t>(i)]() == i * 10);
        }
        SIM_CHECK(v[9]() == 77);
    }

    VSTD::SetDefaultHeap(nullptr);
}

// A callable in a heap block follows the block when Defragment() moves it. Assigning an empty Function frees the
// block, so the heap has less used space afterwards.
void HeapStoredCallableSurvivesDefragment() {
    static uint8_t buffer[1024];
    Memory::Heap<Memory::IncrementalDefrag<4>> heap(buffer, sizeof(buffer));

    Memory::Handle front = heap.Allocate(64);
    std::array<int, 16> values{};
    values[15] = 11;
    Memory::AllocatorOf<Memory::Heap<Memory::IncrementalDefrag<4>>> adapter(heap);
    VSTD::Function<int()> f(&adapter, [values]() { return values[15]; });
    SIM_CHECK(static_cast<bool>(f));
    Memory::Handle back = heap.Allocate(64);
    SIM_CHECK(back.IsValid());

    heap.Deallocate(front);
    heap.Defragment();
    SIM_CHECK(f() == 11);

    std::size_t usedWithCallable = heap.UsedBytes();
    f = VSTD::Function<int()>();
    SIM_CHECK(!static_cast<bool>(f));
    SIM_CHECK(heap.UsedBytes() < usedWithCallable);
}

}

int main() {
    InjectedAllocatorServesTheBlock();
    HeapStoredCallableSurvivesDefragment();
    BlockIsFreedToTheAllocatorThatMadeIt();
    RefusedAllocationIsVisible();
    EmptyCallableIsDefined();
    SmallCallableMoves();
    CallablesSurviveVectorGrowth();
    return VstdSim::Finish("TestFunction");
}
