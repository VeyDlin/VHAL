#pragma once

#include <Utilities/Memory/Handle.h>
#include <Utilities/Memory/IAllocator.h>

#include <cstddef>
#include <cstdlib>
#include <cstring>


// Allocates each block with malloc and frees it with free, so a pointer into a freed block is visible to AddressSanitizer.
class MallocAllocator : public Memory::IAllocator {
public:
	Memory::Handle Allocate(std::size_t size) override {
		Memory::Entry* entry = new Memory::Entry;
		entry->Activate(std::malloc(size != 0 ? size : 1), size);
		return Memory::Handle(entry);
	}

	void Deallocate(Memory::Handle& handle) override {
		if (!handle.IsValid()) {
			return;
		}
		Memory::Entry* entry = handle.GetEntry();
		std::free(entry->data);
		handle.Invalidate();
		delete entry;
	}

	Memory::Handle Reallocate(Memory::Handle& handle, std::size_t newSize) override {
		if (!handle.IsValid()) {
			return Allocate(newSize);
		}
		Memory::Entry* entry = handle.GetEntry();
		void* fresh = std::malloc(newSize != 0 ? newSize : 1);
		std::memcpy(fresh, entry->data, entry->size < newSize ? entry->size : newSize);
		std::free(entry->data);
		entry->data = fresh;
		entry->size = newSize;
		return handle;
	}
};


// Forwards every call to an inner allocator, unless refusing is set. While it is set, Allocate and Reallocate return an empty
// handle and the inner allocator is not called; Deallocate always forwards.
class RefusingAllocator : public Memory::IAllocator {
public:
	bool refusing = false;

private:
	Memory::IAllocator& inner_;

public:
	explicit RefusingAllocator(Memory::IAllocator& inner) : inner_(inner) {}

	Memory::Handle Allocate(std::size_t size) override {
		if (refusing) {
			return Memory::Handle();
		}
		return inner_.Allocate(size);
	}

	void Deallocate(Memory::Handle& handle) override {
		inner_.Deallocate(handle);
	}

	Memory::Handle Reallocate(Memory::Handle& handle, std::size_t newSize) override {
		if (refusing) {
			return Memory::Handle();
		}
		return inner_.Reallocate(handle, newSize);
	}
};
