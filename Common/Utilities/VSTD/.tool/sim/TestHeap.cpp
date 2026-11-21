#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <Utilities/VSTD/VSTD.h>
#include "SimCheck.h"

namespace {

// Takes 64-byte blocks until the heap refuses one. Returns how many were taken.
std::size_t FillWithBlocks(VSTD::Heap<>& heap, VSTD::Handle* blocks, std::size_t maxBlocks) {
    std::size_t count = 0;
    while (count < maxBlocks) {
        VSTD::Handle block = heap.Allocate(64);
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
    VSTD::Heap<> heap(buffer, sizeof(buffer));
    VSTD::Handle blocks[32];
    std::size_t count = FillWithBlocks(heap, blocks, 32);
    SIM_CHECK(count >= 4);

    VSTD::Handle first = blocks[1];
    VSTD::Handle second = blocks[2];
    heap.Deallocate(first);
    heap.Deallocate(second);

    VSTD::Handle merged = heap.Allocate(120);
    SIM_CHECK(merged.IsValid());
}

// A free block at the end of the used region merges with the free block before it, and the merged space is reused.
void FreeTailBlocksMergeOnFree() {
    static uint8_t buffer[1024];
    VSTD::Heap<> heap(buffer, sizeof(buffer));
    VSTD::Handle blocks[32];
    std::size_t count = FillWithBlocks(heap, blocks, 32);
    SIM_CHECK(count >= 4);

    VSTD::Handle penultimate = blocks[count - 2];
    VSTD::Handle last = blocks[count - 1];
    heap.Deallocate(penultimate);
    heap.Deallocate(last);

    VSTD::Handle merged = heap.Allocate(120);
    SIM_CHECK(merged.IsValid());
}

// Freeing every block in any order leaves the whole data region free, so one large block fits again.
void FreeEverythingInAnyOrderFreesTheRegion() {
    static uint8_t buffer[1024];
    VSTD::Heap<> heap(buffer, sizeof(buffer));
    VSTD::Handle blocks[32];
    std::size_t count = FillWithBlocks(heap, blocks, 32);

    for (std::size_t i = 0; i < count; i += 2) {
        heap.Deallocate(blocks[i]);
    }
    for (std::size_t i = 1; i < count; i += 2) {
        heap.Deallocate(blocks[i]);
    }
    SIM_CHECK(heap.UsedBytes() == 0);

    VSTD::Handle large = heap.Allocate(600);
    SIM_CHECK(large.IsValid());
}

// The default policy never moves a block, so a block keeps its address across Defragment().
void DefaultPolicyKeepsBlocksInPlace() {
    static uint8_t buffer[1024];
    VSTD::Heap<> heap(buffer, sizeof(buffer));

    VSTD::Handle a = heap.Allocate(64);
    VSTD::Handle b = heap.Allocate(64);
    VSTD::Handle c = heap.Allocate(64);
    void* before = c.Data();
    heap.Deallocate(a);
    heap.Deallocate(b);

    heap.Defragment();
    SIM_CHECK(c.Data() == before);
}

// Defragment() moves a block. The raw pointer taken before the move goes stale, and the handle keeps the data.
void DefragmentMovesBlocksAndOnlyHandlesFollow() {
    static uint8_t buffer[1024];
    VSTD::Heap<VSTD::IncrementalDefrag<4>> heap(buffer, sizeof(buffer));

    VSTD::Handle a = heap.Allocate(64);
    VSTD::Handle b = heap.Allocate(64);
    VSTD::Handle c = heap.Allocate(64);
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
            VSTD::Heap<> heap(arena + skip, size);

            VSTD::Handle first = heap.Allocate(40);
            VSTD::Handle second = heap.Allocate(72);
            SIM_CHECK(first.IsValid());
            SIM_CHECK(second.IsValid());
            SIM_CHECK(reinterpret_cast<std::uintptr_t>(first.GetEntry()) % alignof(VSTD::Entry) == 0);
            SIM_CHECK(reinterpret_cast<std::uintptr_t>(second.GetEntry()) % alignof(VSTD::Entry) == 0);
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
            VSTD::Heap<> heap(arena + skip, size);
            SIM_CHECK(heap.TotalBytes() <= size);
            SIM_CHECK(heap.TotalBytes() % alignof(VSTD::Entry) == 0);
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
    VSTD::Heap<> heap(buffer, sizeof(buffer));

    std::size_t failures = 0;
    for (int cycle = 0; cycle < 10000; cycle++) {
        VSTD::Handle block = heap.Allocate(16);
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
    VSTD::Heap<> heap(buffer, sizeof(buffer));
    const std::size_t maxSize = std::numeric_limits<std::size_t>::max();

    SIM_CHECK(!heap.Allocate(maxSize).IsValid());
    SIM_CHECK(!heap.Allocate(maxSize - 3).IsValid());
    SIM_CHECK(!heap.Allocate(heap.TotalBytes() + 1).IsValid());
    SIM_CHECK(heap.UsedBytes() == 0);
}

// Reallocate takes the lock once and never nests it. A nested lock would deadlock a mutex-backed policy.
void ReallocateTakesTheLockOnce() {
    static uint8_t buffer[1024];
    VSTD::Heap<VSTD::NoDefrag, CountingLock> heap(buffer, sizeof(buffer));

    VSTD::Handle block = heap.Allocate(32);
    CountingLock::acquisitions = 0;
    VSTD::Handle moved = heap.Reallocate(block, 64);

    SIM_CHECK(moved.IsValid());
    SIM_CHECK(!CountingLock::nested);
    SIM_CHECK(CountingLock::acquisitions == 1);
}

// Random allocate, free, reallocate and defragment steps. Every live block keeps its bytes, and UsedBytes is the
// sum of the live blocks.
void RandomOperationsKeepContentAndAccounting() {
    static uint8_t buffer[2048];
    VSTD::Heap<VSTD::IncrementalDefrag<2>> heap(buffer, sizeof(buffer));

    constexpr std::size_t kSlots = 12;
    VSTD::Handle slots[kSlots];
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
                    VSTD::Handle block = heap.Allocate(1 + next() % 96);
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
                    VSTD::Handle moved = heap.Reallocate(slots[index], oldSize + 1 + next() % 64);
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
    return VstdSim::Finish("TestHeap");
}
