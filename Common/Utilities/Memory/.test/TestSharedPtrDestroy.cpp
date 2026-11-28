#include <VhalCheck.h>
#include <Utilities/Memory/SharedPtr.h>

#include "AllocatorDoubles.h"

#include <cstddef>


// Host tests of the lifetime of a shared object: the object is constructed once and destroyed once, when its last copy is released.


namespace {

// Counts the live objects, so a test can see when the shared object is constructed and destroyed.
struct Counted {
	static inline int live = 0;
	int value;

	explicit Counted(int v) : value(v) {
		live++;
	}

	~Counted() {
		live--;
	}
};

}


void SharedObjectDestroyedOnceWithLastCopy() {
	Counted::live = 0;
	{
		MallocAllocator heap;
		Memory::SharedPtr<Counted> a = Memory::AllocateShared<Counted>(&heap, 12);
		VHAL_CHECK_EQ(a.UseCount(), static_cast<std::size_t>(1));
		{
			Memory::SharedPtr<Counted> b = a;
			VHAL_CHECK_EQ(a.UseCount(), static_cast<std::size_t>(2));
			VHAL_CHECK_EQ(b->value, 12);
		}
		VHAL_CHECK_EQ(a.UseCount(), static_cast<std::size_t>(1));
		VHAL_CHECK_EQ(Counted::live, 1);
	}
	VHAL_CHECK_EQ(Counted::live, 0);
}


int main() {
	SharedObjectDestroyedOnceWithLastCopy();

	return VhalCheck::Finish("TestSharedPtrDestroy");
}
