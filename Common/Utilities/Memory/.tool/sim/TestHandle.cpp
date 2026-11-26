#include <cstdint>
#include <Utilities/Memory/Heap.h>
#include <Utilities/Memory/PoolAllocator.h>
#include "SimCheck.h"

namespace {

// A copy of a handle taken before its block was freed must not match the block that reuses the same entry slot.
// Blocks on both sides of owner keep its space from merging into the unallocated tail, so its slot is the one reused.
void HeapStaleCopyStaysDead() {
    static uint8_t buffer[1024];
    Memory::Heap<> heap(buffer, sizeof(buffer));

    Memory::Handle keeper = heap.Allocate(32);
    Memory::Handle owner = heap.Allocate(32);
    Memory::Handle after = heap.Allocate(32);
    Memory::Entry* slot = owner.GetEntry();
    Memory::Handle stale = owner;
    heap.Deallocate(owner);
    SIM_CHECK(!stale.IsValid());

    Memory::Handle reuser = heap.Allocate(32);
    SIM_CHECK(reuser.IsValid());
    SIM_CHECK(reuser.GetEntry() == slot);
    SIM_CHECK(!stale.IsValid());
    SIM_CHECK(stale.Data() == nullptr);
    SIM_CHECK(stale.Size() == 0);

    heap.Deallocate(stale);
    SIM_CHECK(reuser.IsValid());
    SIM_CHECK(keeper.IsValid());
    SIM_CHECK(after.IsValid());
    SIM_CHECK(heap.UsedBytes() == 96);

    stale.Invalidate();
    SIM_CHECK(reuser.IsValid());
}

void PoolStaleCopyStaysDead() {
    Memory::PoolAllocator<32, 2> pool;

    Memory::Handle owner = pool.Allocate(16);
    Memory::Entry* slot = owner.GetEntry();
    Memory::Handle stale = owner;
    pool.Deallocate(owner);

    Memory::Handle reuser = pool.Allocate(16);
    SIM_CHECK(reuser.IsValid());
    SIM_CHECK(reuser.GetEntry() == slot);
    SIM_CHECK(!stale.IsValid());

    pool.Deallocate(stale);
    SIM_CHECK(reuser.IsValid());
    SIM_CHECK(pool.AllocatedCount() == 1);

    stale.Invalidate();
    SIM_CHECK(reuser.IsValid());
}

// Invalidate clears only the copy it is called on. The block stays allocated until Deallocate frees it.
void HeapInvalidateKeepsTheBlock() {
    static uint8_t buffer[1024];
    Memory::Heap<> heap(buffer, sizeof(buffer));

    Memory::Handle owner = heap.Allocate(32);
    Memory::Handle copy = owner;
    owner.Invalidate();
    SIM_CHECK(!owner.IsValid());
    SIM_CHECK(copy.IsValid());
    SIM_CHECK(heap.UsedBytes() == 32);

    heap.Deallocate(copy);
    SIM_CHECK(!copy.IsValid());
    SIM_CHECK(heap.UsedBytes() == 0);
}

void PoolInvalidateKeepsTheBlock() {
    Memory::PoolAllocator<32, 2> pool;

    Memory::Handle owner = pool.Allocate(16);
    Memory::Handle copy = owner;
    owner.Invalidate();
    SIM_CHECK(copy.IsValid());
    SIM_CHECK(pool.AllocatedCount() == 1);

    pool.Deallocate(copy);
    SIM_CHECK(!copy.IsValid());
    SIM_CHECK(pool.AllocatedCount() == 0);
}

}

int main() {
    HeapStaleCopyStaysDead();
    PoolStaleCopyStaysDead();
    HeapInvalidateKeepsTheBlock();
    PoolInvalidateKeepsTheBlock();
    return VstdSim::Finish("TestHandle");
}
