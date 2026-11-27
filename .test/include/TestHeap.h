#pragma once
// A heap for the host tests that is also the allocator of the network clients. The clients take a Memory::IAllocator&, and a
// heap of a template type is not one. TestHeap is a Memory::Heap with the allocator interface added: Allocate, Deallocate and
// Reallocate forward to the heap, and every other member is the heap's own.
#include <VHAL.h>
#include <Utilities/Memory/Heap.h>
#include <Utilities/Memory/IAllocator.h>
#include <cstddef>


template <typename DefragPolicy = Memory::NoDefrag>
class TestHeap : public Memory::Heap<DefragPolicy>, public Memory::IAllocator {
public:
	using Memory::Heap<DefragPolicy>::Heap;


	Memory::Handle Allocate(std::size_t size) override {
		return Memory::Heap<DefragPolicy>::Allocate(size);
	}


	void Deallocate(Memory::Handle& handle) override {
		Memory::Heap<DefragPolicy>::Deallocate(handle);
	}


	Memory::Handle Reallocate(Memory::Handle& handle, std::size_t newSize) override {
		return Memory::Heap<DefragPolicy>::Reallocate(handle, newSize);
	}
};
