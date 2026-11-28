#include <VhalCheck.h>
#include <Utilities/Memory/Heap.h>

#include "SimCheck.h"

#include <cstddef>
#include <cstdint>
#include <cstring>


// Defragment keeps the data of every live block, and the space the freed blocks released is free afterwards: a request past the
// size of the heap is refused, and the 64 bytes released before the move can be allocated again. The policy moves blocks, so the
// move is what the check exercises.
void HeapDefragmentKeepsDataAndFreeSpace() {
	alignas(16) static std::uint8_t storage[1024];
	Memory::Heap<Memory::IncrementalDefrag<4>> heap(storage);

	Memory::Handle first = heap.Allocate(64);
	Memory::Handle second = heap.Allocate(200);
	Memory::Handle third = heap.Allocate(100);
	VHAL_CHECK(first.IsValid() && second.IsValid() && third.IsValid());

	std::memset(second.Data(), 0x5A, 200);
	std::memset(third.Data(), 0xC3, 100);
	heap.Deallocate(first);
	heap.Defragment();

	const std::uint8_t* secondBytes = static_cast<const std::uint8_t*>(second.Data());
	const std::uint8_t* thirdBytes = static_cast<const std::uint8_t*>(third.Data());
	bool secondIntact = true;
	bool thirdIntact = true;
	for (std::size_t i = 0; i < 200; i++) {
		secondIntact = secondIntact && secondBytes[i] == 0x5A;
	}
	for (std::size_t i = 0; i < 100; i++) {
		thirdIntact = thirdIntact && thirdBytes[i] == 0xC3;
	}
	VHAL_CHECK(secondIntact);
	VHAL_CHECK(thirdIntact);

	// If the freed block's size were the moved bytes (0x5A...), the heap would hand out memory it does not have.
	Memory::Handle tooBig = heap.Allocate(1000);
	VHAL_CHECK(!tooBig.IsValid());

	// The 64 bytes released before the move must be usable again.
	Memory::Handle reuse = heap.Allocate(64);
	VHAL_CHECK(reuse.IsValid());
}


int main() {
	HeapDefragmentKeepsDataAndFreeSpace();

	return VhalCheck::Finish("TestHeapDefragFreeSpace");
}
