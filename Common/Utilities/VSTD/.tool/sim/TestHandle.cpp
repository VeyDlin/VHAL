#include <cstdint>
#include <Utilities/VSTD/VSTD.h>
#include "SimCheck.h"

namespace {

// A copy of a handle taken before its block was freed must not match the block that reuses the same entry slot.
// Blocks on both sides of owner keep its space from merging into the unallocated tail, so its slot is the one reused.
void HeapStaleCopyStaysDead() {
    static uint8_t buffer[1024];
    VSTD::Heap<> heap(buffer, sizeof(buffer));

    VSTD::Handle keeper = heap.Allocate(32);
    VSTD::Handle owner = heap.Allocate(32);
    VSTD::Handle after = heap.Allocate(32);
    VSTD::Entry* slot = owner.GetEntry();
    VSTD::Handle stale = owner;
    heap.Deallocate(owner);
    SIM_CHECK(!stale.IsValid());

    VSTD::Handle reuser = heap.Allocate(32);
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
    VSTD::PoolAllocator<32, 2> pool;

    VSTD::Handle owner = pool.Allocate(16);
    VSTD::Entry* slot = owner.GetEntry();
    VSTD::Handle stale = owner;
    pool.Deallocate(owner);

    VSTD::Handle reuser = pool.Allocate(16);
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
    VSTD::Heap<> heap(buffer, sizeof(buffer));

    VSTD::Handle owner = heap.Allocate(32);
    VSTD::Handle copy = owner;
    owner.Invalidate();
    SIM_CHECK(!owner.IsValid());
    SIM_CHECK(copy.IsValid());
    SIM_CHECK(heap.UsedBytes() == 32);

    heap.Deallocate(copy);
    SIM_CHECK(!copy.IsValid());
    SIM_CHECK(heap.UsedBytes() == 0);
}

void PoolInvalidateKeepsTheBlock() {
    VSTD::PoolAllocator<32, 2> pool;

    VSTD::Handle owner = pool.Allocate(16);
    VSTD::Handle copy = owner;
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
