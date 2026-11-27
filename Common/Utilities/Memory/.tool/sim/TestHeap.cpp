#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <Utilities/Memory/Heap.h>
#include <Utilities/Memory/PoolAllocator.h>
#include "SimCheck.h"

namespace {

// Takes 64-byte blocks until the heap refuses one. Returns how many were taken.
std::size_t FillWithBlocks(Memory::Heap<>& heap, Memory::Handle* blocks, std::size_t maxBlocks) {
    std::size_t count = 0;
    while (count < maxBlocks) {
        Memory::Handle block = heap.Allocate(64);
        if (!block.IsValid()) {
            break;
        }
        blocks[count] = block;
        count++;
    }
    return count;
}

// Two adjacent free blocks merge when they are freed, so a request for their combined size succeeds without Defragment().
void AdjacentFreeBlocksMergeOnFree() {
    static uint8_t buffer[1024];
    Memory::Heap<> heap(buffer, sizeof(buffer));
    Memory::Handle blocks[32];
    std::size_t count = FillWithBlocks(heap, blocks, 32);
    SIM_CHECK(count >= 4);

    Memory::Handle first = blocks[1];
    Memory::Handle second = blocks[2];
    heap.Deallocate(first);
    heap.Deallocate(second);

    Memory::Handle merged = heap.Allocate(120);
    SIM_CHECK(merged.IsValid());
}

// A free block at the end of the used region merges with the free block before it, and the merged space is reused.
void FreeTailBlocksMergeOnFree() {
    static uint8_t buffer[1024];
    Memory::Heap<> heap(buffer, sizeof(buffer));
    Memory::Handle blocks[32];
    std::size_t count = FillWithBlocks(heap, blocks, 32);
    SIM_CHECK(count >= 4);

    Memory::Handle penultimate = blocks[count - 2];
    Memory::Handle last = blocks[count - 1];
    heap.Deallocate(penultimate);
    heap.Deallocate(last);

    Memory::Handle merged = heap.Allocate(120);
    SIM_CHECK(merged.IsValid());
}

// Freeing every block in any order leaves the whole data region free, so one large block fits again.
void FreeEverythingInAnyOrderFreesTheRegion() {
    static uint8_t buffer[1024];
    Memory::Heap<> heap(buffer, sizeof(buffer));
    Memory::Handle blocks[32];
    std::size_t count = FillWithBlocks(heap, blocks, 32);

    for (std::size_t i = 0; i < count; i += 2) {
        heap.Deallocate(blocks[i]);
    }
    for (std::size_t i = 1; i < count; i += 2) {
        heap.Deallocate(blocks[i]);
    }
    SIM_CHECK(heap.UsedBytes() == 0);

    Memory::Handle large = heap.Allocate(600);
    SIM_CHECK(large.IsValid());
}

// The default policy never moves a block, so a block keeps its address across Defragment().
void DefaultPolicyKeepsBlocksInPlace() {
    static uint8_t buffer[1024];
    Memory::Heap<> heap(buffer, sizeof(buffer));

    Memory::Handle a = heap.Allocate(64);
    Memory::Handle b = heap.Allocate(64);
    Memory::Handle c = heap.Allocate(64);
    void* before = c.Data();
    heap.Deallocate(a);
    heap.Deallocate(b);

    heap.Defragment();
    SIM_CHECK(c.Data() == before);
}

// Defragment() moves a block. The raw pointer taken before the move goes stale, and the handle keeps the data.
void DefragmentMovesBlocksAndOnlyHandlesFollow() {
    static uint8_t buffer[1024];
    Memory::Heap<Memory::IncrementalDefrag<4>> heap(buffer, sizeof(buffer));

    Memory::Handle a = heap.Allocate(64);
    Memory::Handle b = heap.Allocate(64);
    Memory::Handle c = heap.Allocate(64);
    std::memset(c.Data(), 0x5A, 64);
    uint8_t* raw = static_cast<uint8_t*>(c.Data());

    heap.Deallocate(a);
    heap.Deallocate(b);
    heap.Defragment();

    SIM_CHECK(c.IsValid());
    SIM_CHECK(c.Data() != static_cast<void*>(raw));
    SIM_CHECK(static_cast<uint8_t*>(c.Data())[0] == 0x5A);
    SIM_CHECK(static_cast<uint8_t*>(c.Data())[63] == 0x5A);
}

// Entries sit at the end of the buffer. Their address must be aligned for any capacity and any buffer start.
void EntriesAreAlignedForAnyCapacity() {
    alignas(16) static uint8_t arena[1100];

    for (std::size_t skip = 0; skip < 4; skip++) {
        for (std::size_t size = 1000; size < 1008; size++) {
            Memory::Heap<> heap(arena + skip, size);

            Memory::Handle first = heap.Allocate(40);
            Memory::Handle second = heap.Allocate(72);
            SIM_CHECK(first.IsValid());
            SIM_CHECK(second.IsValid());
            SIM_CHECK(reinterpret_cast<std::uintptr_t>(first.GetEntry()) % alignof(Memory::Entry) == 0);
            SIM_CHECK(reinterpret_cast<std::uintptr_t>(second.GetEntry()) % alignof(Memory::Entry) == 0);
            SIM_CHECK(reinterpret_cast<std::uintptr_t>(first.Data()) % alignof(void*) == 0);
            SIM_CHECK(reinterpret_cast<std::uintptr_t>(second.Data()) % alignof(void*) == 0);
        }
    }
}

// TotalBytes() is the usable capacity: a whole number of entry slots, and never more than the buffer size.
void TotalBytesIsTheUsableCapacity() {
    alignas(16) static uint8_t arena[1100];

    for (std::size_t skip = 0; skip < 4; skip++) {
        for (std::size_t size = 1000; size < 1008; size++) {
            Memory::Heap<> heap(arena + skip, size);
            SIM_CHECK(heap.TotalBytes() <= size);
            SIM_CHECK(heap.TotalBytes() % alignof(Memory::Entry) == 0);
        }
    }
}

// A lock policy that counts the acquisitions and notices when the lock is taken while it is already held.
struct CountingLock {
    static inline int acquisitions = 0;
    static inline bool held = false;
    static inline bool nested = false;

    static void Lock() {
        if (held) {
            nested = true;
        }
        held = true;
        acquisitions++;
    }

    static void Unlock() {
        held = false;
    }
};

// Each tail allocation that is freed again reuses the entry slot it had. The entry table must not grow with each cycle.
void TailChurnDoesNotExhaustTheEntryTable() {
    static uint8_t buffer[4096];
    Memory::Heap<> heap(buffer, sizeof(buffer));

    std::size_t failures = 0;
    for (int cycle = 0; cycle < 10000; cycle++) {
        Memory::Handle block = heap.Allocate(16);
        if (!block.IsValid()) {
            failures++;
            break;
        }
        heap.Deallocate(block);
    }
    SIM_CHECK(failures == 0);
    SIM_CHECK(heap.UsedBytes() == 0);
}

// A request larger than the heap is refused, whatever its size. No size wraps around to a small block.
void OversizedRequestsAreRefused() {
    static uint8_t buffer[1024];
    Memory::Heap<> heap(buffer, sizeof(buffer));
    const std::size_t maxSize = std::numeric_limits<std::size_t>::max();

    SIM_CHECK(!heap.Allocate(maxSize).IsValid());
    SIM_CHECK(!heap.Allocate(maxSize - 3).IsValid());
    SIM_CHECK(!heap.Allocate(heap.TotalBytes() + 1).IsValid());
    SIM_CHECK(heap.UsedBytes() == 0);
}

// Reallocate takes the lock once and never nests it. A nested lock would deadlock a mutex-backed policy.
void ReallocateTakesTheLockOnce() {
    static uint8_t buffer[1024];
    Memory::Heap<Memory::NoDefrag, CountingLock> heap(buffer, sizeof(buffer));

    Memory::Handle block = heap.Allocate(32);
    CountingLock::acquisitions = 0;
    Memory::Handle moved = heap.Reallocate(block, 64);

    SIM_CHECK(moved.IsValid());
    SIM_CHECK(!CountingLock::nested);
    SIM_CHECK(CountingLock::acquisitions == 1);
}

// Random allocate, free, reallocate and defragment steps. Every live block keeps its bytes, and UsedBytes is the
// sum of the live blocks.
void RandomOperationsKeepContentAndAccounting() {
    static uint8_t buffer[2048];
    Memory::Heap<Memory::IncrementalDefrag<2>> heap(buffer, sizeof(buffer));

    constexpr std::size_t kSlots = 12;
    Memory::Handle slots[kSlots];
    uint8_t tags[kSlots] = {};
    std::uint32_t state = 12345;
    auto next = [&state]() -> std::uint32_t {
        state = state * 1664525u + 1013904223u;
        return state >> 8;
    };

    std::size_t violations = 0;
    for (int step = 0; step < 4000; step++) {
        std::size_t index = next() % kSlots;
        switch (next() % 4) {
            case 0:
                if (!slots[index].IsValid()) {
                    Memory::Handle block = heap.Allocate(1 + next() % 96);
                    if (block.IsValid()) {
                        tags[index] = static_cast<uint8_t>(1 + next() % 255);
                        std::memset(block.Data(), tags[index], block.Size());
                        slots[index] = block;
                    }
                }
                break;
            case 1:
                heap.Deallocate(slots[index]);
                break;
            case 2:
                if (slots[index].IsValid()) {
                    std::size_t oldSize = slots[index].Size();
                    Memory::Handle moved = heap.Reallocate(slots[index], oldSize + 1 + next() % 64);
                    if (moved.IsValid()) {
                        std::memset(static_cast<uint8_t*>(moved.Data()) + oldSize, tags[index], moved.Size() - oldSize);
                        slots[index] = moved;
                    }
                }
                break;
            default:
                heap.Defragment();
                break;
        }

        std::size_t liveBytes = 0;
        for (std::size_t i = 0; i < kSlots; i++) {
            if (!slots[i].IsValid()) {
                continue;
            }
            liveBytes += slots[i].Size();
            const uint8_t* bytes = static_cast<const uint8_t*>(slots[i].Data());
            for (std::size_t b = 0; b < slots[i].Size(); b++) {
                if (bytes[b] != tags[i]) {
                    violations++;
                    break;
                }
            }
        }
        if (liveBytes != heap.UsedBytes()) {
            violations++;
        }
    }
    SIM_CHECK(violations == 0);
}

int outOfMemoryCalls = 0;

void CountOutOfMemory() {
    outOfMemoryCalls++;
}

// A refused request calls the out-of-memory policy once, and a granted request does not. A refused reallocation
// leaves the block it was given.
void OutOfMemoryPolicyIsCalledOnRefusal() {
    static uint8_t buffer[256];
    Memory::Heap<Memory::NoDefrag, Memory::NoLock, Memory::OomHandler<&CountOutOfMemory>> heap(buffer, sizeof(buffer));

    outOfMemoryCalls = 0;
    Memory::Handle granted = heap.Allocate(32);
    SIM_CHECK(granted.IsValid());
    SIM_CHECK(outOfMemoryCalls == 0);

    Memory::Handle refused = heap.Allocate(4096);
    SIM_CHECK(!refused.IsValid());
    SIM_CHECK(outOfMemoryCalls == 1);

    Memory::Handle refusedGrowth = heap.Reallocate(granted, 4096);
    SIM_CHECK(!refusedGrowth.IsValid());
    SIM_CHECK(outOfMemoryCalls == 2);
    SIM_CHECK(granted.IsValid());
}

// The data of every block is 8-byte aligned, from any buffer start, whatever the sizes and the order of allocation,
// freeing and defragmenting.
void DataIsEightByteAlignedOnEveryBlock() {
    static uint8_t buffer[1024 + 3];
    Memory::Heap<Memory::IncrementalDefrag<2>> heap(buffer + 1, sizeof(buffer) - 1);
    Memory::Handle live[8];

    std::uint32_t seed = 7;
    for (int step = 0; step < 400; step++) {
        seed = seed * 1103515245u + 12345u;
        std::size_t slot = (seed >> 16) % 8;
        if (live[slot].IsValid()) {
            heap.Deallocate(live[slot]);
        } else {
            live[slot] = heap.Allocate(1 + (seed >> 8) % 60);
        }
        heap.Defragment();

        for (const Memory::Handle& handle : live) {
            if (handle.IsValid()) {
                SIM_CHECK(reinterpret_cast<std::uintptr_t>(handle.Data()) % 8 == 0);
            }
        }
    }
}

// An empty heap has all of its bytes free. A block takes its bytes from the used count, and freeing it again leaves
// the heap as it was, less the entry slot that stays in the table.
void FreeBytesAccountForTheWholeHeap() {
    static uint8_t buffer[512];
    Memory::Heap<> heap(buffer, sizeof(buffer));
    SIM_CHECK(heap.FreeBytes() == heap.TotalBytes());

    Memory::Handle block = heap.Allocate(64);
    SIM_CHECK(heap.UsedBytes() == 64);
    SIM_CHECK(heap.FreeBytes() < heap.TotalBytes() - 64);

    heap.Deallocate(block);
    SIM_CHECK(heap.UsedBytes() == 0);
    SIM_CHECK(heap.FreeBytes() == heap.TotalBytes() - sizeof(Memory::Entry));
}

// A hole between two used blocks is fragmentation. Free space at the end of the used data is not.
void FragmentationCountsTheHoleBetweenBlocks() {
    static uint8_t buffer[1024];
    Memory::Heap<> heap(buffer, sizeof(buffer));
    Memory::Handle first = heap.Allocate(64);
    Memory::Handle second = heap.Allocate(64);
    Memory::Handle third = heap.Allocate(64);
    SIM_CHECK(heap.FragmentationPercent() == 0);

    heap.Deallocate(third);
    SIM_CHECK(heap.FragmentationPercent() == 0);

    heap.Deallocate(first);
    SIM_CHECK(heap.FragmentationPercent() > 0);
    SIM_CHECK(heap.FragmentationPercent() <= 100);

    heap.Deallocate(second);
    SIM_CHECK(heap.FragmentationPercent() == 0);
}

// A pinned block keeps its address and its bytes through Defragment(). Deallocate() and Reallocate() refuse it, and
// ReleasePinned() frees it. ReleasePinned() ignores a pointer that is not a pinned block.
void PinnedBlocksStayAndRefuseHandleOperations() {
    static uint8_t buffer[1024];
    Memory::Heap<Memory::IncrementalDefrag<4>> heap(buffer, sizeof(buffer));

    Memory::Handle front = heap.Allocate(64);
    Memory::Handle pinned = heap.AllocatePinnedHandle(48, 8);
    Memory::Handle back = heap.Allocate(64);
    SIM_CHECK(pinned.IsValid());
    SIM_CHECK(back.IsValid());

    void* pinnedAddress = pinned.Data();
    std::memset(pinnedAddress, 0x5A, 48);

    heap.Deallocate(front);
    heap.Defragment();
    SIM_CHECK(pinned.Data() == pinnedAddress);
    SIM_CHECK(static_cast<uint8_t*>(pinnedAddress)[47] == 0x5A);

    heap.Deallocate(pinned);
    SIM_CHECK(pinned.IsValid());

    Memory::Handle grown = heap.Reallocate(pinned, 128);
    SIM_CHECK(!grown.IsValid());
    SIM_CHECK(pinned.IsValid());

    heap.ReleasePinned(back.Data());
    SIM_CHECK(back.IsValid());

    heap.ReleasePinned(pinnedAddress);
    SIM_CHECK(!pinned.IsValid());
    SIM_CHECK(heap.UsedBytes() == 64);
}

// AllocatePinned() honours the alignment it is given, up to 64 bytes. An alignment that is not a power of two, or is
// larger than 64, is refused.
void PinnedBlocksHonourTheirAlignment() {
    static uint8_t buffer[1024];
    Memory::Heap<> heap(buffer, sizeof(buffer));

    void* aligned = heap.AllocatePinned(40, 32);
    SIM_CHECK(aligned != nullptr);
    SIM_CHECK(reinterpret_cast<std::uintptr_t>(aligned) % 32 == 0);
    SIM_CHECK(heap.AllocatePinned(8, 3) == nullptr);
    SIM_CHECK(heap.AllocatePinned(8, 128) == nullptr);

    heap.ReleasePinned(aligned);
    SIM_CHECK(heap.UsedBytes() == 0);
}

// A handle of another heap, or of a pool, is left alone by Deallocate() and is copied by Reallocate(), as a handle that
// this heap does not know is. Neither heap's blocks change.
void ForeignHandlesAreIgnored() {
    static uint8_t firstBuffer[512];
    static uint8_t secondBuffer[512];
    Memory::Heap<> first(firstBuffer, sizeof(firstBuffer));
    Memory::Heap<> second(secondBuffer, sizeof(secondBuffer));

    Memory::Handle mine = first.Allocate(32);
    Memory::Handle theirs = second.Allocate(32);
    SIM_CHECK(mine.IsValid());
    SIM_CHECK(theirs.IsValid());
    const std::size_t firstUsed = first.UsedBytes();
    const std::size_t secondUsed = second.UsedBytes();

    first.Deallocate(theirs);
    SIM_CHECK(theirs.IsValid());
    SIM_CHECK(first.UsedBytes() == firstUsed);
    SIM_CHECK(second.UsedBytes() == secondUsed);
    SIM_CHECK(mine.IsValid());

    Memory::PoolAllocator<32, 4> pool;
    Memory::Handle pooled = pool.Allocate(32);
    SIM_CHECK(pooled.IsValid());

    first.Deallocate(pooled);
    SIM_CHECK(pooled.IsValid());
    SIM_CHECK(first.UsedBytes() == firstUsed);

    Memory::Handle copy = first.Reallocate(pooled, 64);
    SIM_CHECK(copy.IsValid());
    SIM_CHECK(pooled.IsValid());
    SIM_CHECK(first.UsedBytes() == firstUsed + 64);

    first.Deallocate(copy);
    SIM_CHECK(first.UsedBytes() == firstUsed);
    SIM_CHECK(mine.IsValid());
    SIM_CHECK(theirs.IsValid());
}

// The heap object reports that its constructor ran.
void HeapReportsItsConstruction() {
    static uint8_t buffer[128];
    Memory::Heap<> heap(buffer, sizeof(buffer));
    SIM_CHECK(heap.IsConstructed());
}

}

int main() {
    AdjacentFreeBlocksMergeOnFree();
    FreeTailBlocksMergeOnFree();
    FreeEverythingInAnyOrderFreesTheRegion();
    DefaultPolicyKeepsBlocksInPlace();
    DefragmentMovesBlocksAndOnlyHandlesFollow();
    EntriesAreAlignedForAnyCapacity();
    TotalBytesIsTheUsableCapacity();
    TailChurnDoesNotExhaustTheEntryTable();
    OversizedRequestsAreRefused();
    ReallocateTakesTheLockOnce();
    RandomOperationsKeepContentAndAccounting();
    OutOfMemoryPolicyIsCalledOnRefusal();
    DataIsEightByteAlignedOnEveryBlock();
    FreeBytesAccountForTheWholeHeap();
    FragmentationCountsTheHoleBetweenBlocks();
    PinnedBlocksStayAndRefuseHandleOperations();
    PinnedBlocksHonourTheirAlignment();
    ForeignHandlesAreIgnored();
    HeapReportsItsConstruction();
    return VstdSim::Finish("TestHeap");
}
