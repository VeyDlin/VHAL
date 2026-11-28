#include <VhalCheck.h>
#include <Utilities/Memory/PoolAllocator.h>

#include <cstddef>
#include <cstdint>


// Host tests of the pool's block size: a request above the block size is refused, and the blocks are laid out so that each
// one is aligned for a pointer.


// A block size of 10 is the size of every block: the largest request that fits is 10 bytes, and the pool has two blocks.
void PoolAllocatorBlockSizeIsAMinimum() {
	Memory::PoolAllocator<10, 2> pool;
	Memory::Handle exact = pool.Allocate(10);
	VHAL_CHECK(exact.IsValid());
	Memory::Handle tooBig = pool.Allocate(11);
	VHAL_CHECK(!tooBig.IsValid());
	VHAL_CHECK_EQ(pool.GetBlockSize(), static_cast<std::size_t>(10));

	// Each usable block is at least BlockSize bytes apart, so writing all of it never overlaps
	Memory::Handle second = pool.Allocate(10);
	VHAL_CHECK(second.IsValid());
	std::uintptr_t distance = reinterpret_cast<std::uintptr_t>(second.Data()) -
		reinterpret_cast<std::uintptr_t>(exact.Data());
	VHAL_CHECK(distance >= 10);
}


// A block size that is not a multiple of the pointer size would put later blocks off alignment, so the stride is rounded up.
void PoolAllocatorBlocksAreAlignedForPointerAccess() {
	Memory::PoolAllocator<10, 2> pool;
	Memory::Handle first = pool.Allocate(8);
	Memory::Handle second = pool.Allocate(8);
	VHAL_CHECK(first.IsValid());
	VHAL_CHECK(second.IsValid());
	VHAL_CHECK(reinterpret_cast<std::uintptr_t>(second.Data()) % alignof(void*) == 0);
}


int main() {
	PoolAllocatorBlockSizeIsAMinimum();
	PoolAllocatorBlocksAreAlignedForPointerAccess();

	return VhalCheck::Finish("TestPoolAllocatorBlocks");
}
