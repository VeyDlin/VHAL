#pragma once

#include <System/System.h>
#include <cstddef>
#include <span>

namespace Memory {


// Storage policies for a container. A container takes one as its last template argument and holds the storage as a member:
//
//     typename Policy::template Storage<ElementType, Capacity> storage;
//
// A storage exposes Data(), which points to Capacity elements, and Ready(), which is false when the storage has no memory.
// A container refuses its writes while Ready() is false.


// The elements are a member array of the container. This is the default policy.
struct Inside {
	template <typename ElementType, std::size_t Capacity>
	class Storage {
	public:
		constexpr bool Ready() const {
			return true;
		}


		ElementType* Data() {
			return items;
		}


		const ElementType* Data() const {
			return items;
		}


	private:
		ElementType items[Capacity];
	};
};


// The elements are a pinned block of the heap, which is a static object with a Memory::Heap type. The block is taken when the
// container is constructed and released when it is destroyed. A pinned block does not move, so an interrupt or DMA may use it.
//
// The heap must be constructed before the container. A static container in another translation unit may be constructed first,
// so a debug build asserts that the heap is constructed. A container is therefore a function-local static, or is created after
// the heap is initialised in main.
template <auto& heap>
struct FromHeap {
	template <typename ElementType, std::size_t Capacity>
	class Storage {
	public:
		Storage()
			: items(Allocate()) {
		}


		Storage(const Storage&) = delete;
		Storage& operator=(const Storage&) = delete;


		~Storage() {
			if (items != nullptr) {
				heap.ReleasePinned(items);
			}
		}


		ElementType* Data() {
			return items;
		}


		const ElementType* Data() const {
			return items;
		}


		bool Ready() const {
			return items != nullptr;
		}


	private:
		ElementType* items;


		static ElementType* Allocate() {
			SystemAssert(heap.IsConstructed(), "a FromHeap container is built before its heap");
			return static_cast<ElementType*>(heap.AllocatePinned(sizeof(ElementType) * Capacity, alignof(ElementType)));
		}
	};
};


// The elements are in memory that the caller gives. The span must hold at least Capacity elements, or the storage is not ready.
// The caller keeps the memory for the life of the container.
struct External {
	template <typename ElementType, std::size_t Capacity>
	class Storage {
	public:
		explicit Storage(std::span<ElementType> memory)
			: items(memory.data())
			, size(memory.size()) {
		}


		bool Ready() const {
			return size >= Capacity;
		}


		ElementType* Data() {
			return items;
		}


		const ElementType* Data() const {
			return items;
		}


	private:
		ElementType* items;
		std::size_t size;
	};
};

}
