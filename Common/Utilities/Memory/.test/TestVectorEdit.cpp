#include <VhalCheck.h>
#include <Utilities/Memory/Vector.h>

#include "AllocatorDoubles.h"

#include <cstddef>
#include <vector>


// Host tests of Vector editing and element lifetime: push, pop, insert, erase, copy and move, and a push of an element of the
// vector itself. The growing operations that a refusing allocator fails are in TestVectorRefusal.cpp.


namespace {

// Counts the live objects of its type, so a test can see when the vector constructs and destroys them.
struct Counted {
	static inline int live = 0;
	int value;

	Counted(int v = 0) : value(v) {
		live++;
	}

	Counted(const Counted& other) : value(other.value) {
		live++;
	}

	Counted(Counted&& other) noexcept : value(other.value) {
		live++;
	}

	~Counted() {
		live--;
	}
};

}


void VectorPushPopIndexAndSize() {
	MallocAllocator heap;
	Memory::Vector<int> v(&heap);
	for (int i = 0; i < 10; i++) {
		VHAL_CHECK(v.PushBack(i * 3));
	}
	VHAL_CHECK_EQ(v.Size(), static_cast<std::size_t>(10));
	VHAL_CHECK_EQ(v[4], 12);
	VHAL_CHECK_EQ(*v.Back(), 27);
	v.PopBack();
	VHAL_CHECK_EQ(v.Size(), static_cast<std::size_t>(9));
}


void VectorInsertEraseEraseRangeMatchesModel() {
	MallocAllocator heap;
	Memory::Vector<int> v(&heap);
	std::vector<int> model;
	for (int i = 0; i < 6; i++) {
		VHAL_CHECK(v.PushBack(i));
		model.push_back(i);
	}
	VHAL_CHECK(v.Insert(0, 100));
	model.insert(model.begin(), 100);
	VHAL_CHECK(v.Insert(3, 200));
	model.insert(model.begin() + 3, 200);
	VHAL_CHECK(v.Insert(v.Size(), 300));
	model.insert(model.end(), 300);
	v.Erase(1);
	model.erase(model.begin() + 1);
	v.EraseRange(2, 3);
	model.erase(model.begin() + 2, model.begin() + 5);

	VHAL_CHECK_EQ(v.Size(), model.size());
	for (std::size_t i = 0; i < model.size() && i < v.Size(); i++) {
		VHAL_CHECK_EQ(v[i], model[i]);
	}
}


void VectorCopyAndMoveSemantics() {
	MallocAllocator heap;
	Memory::Vector<int> source(&heap);
	VHAL_CHECK(source.PushBack(1));
	VHAL_CHECK(source.PushBack(2));

	Memory::Vector<int> copy(source);
	copy[0] = 50;
	VHAL_CHECK_EQ(source[0], 1);
	VHAL_CHECK_EQ(copy[0], 50);

	Memory::Vector<int> moved(static_cast<Memory::Vector<int>&&>(source));
	VHAL_CHECK_EQ(moved.Size(), static_cast<std::size_t>(2));
	VHAL_CHECK_EQ(source.Size(), static_cast<std::size_t>(0));

	Memory::Vector<int> assigned(&heap);
	assigned = copy;
	VHAL_CHECK_EQ(assigned[0], 50);
	assigned = static_cast<Memory::Vector<int>&&>(moved);
	VHAL_CHECK_EQ(assigned.Size(), static_cast<std::size_t>(2));
	VHAL_CHECK_EQ(assigned[1], 2);
}


void VectorElementLifetimeMatchesSize() {
	Counted::live = 0;
	{
		MallocAllocator heap;
		Memory::Vector<Counted> v(&heap);
		for (int i = 0; i < 10; i++) {
			VHAL_CHECK(v.PushBack(Counted(i)));
		}
		VHAL_CHECK_EQ(Counted::live, 10);

		VHAL_CHECK(v.Insert(3, Counted(99)));
		VHAL_CHECK_EQ(Counted::live, 11);
		v.Erase(0);
		VHAL_CHECK_EQ(Counted::live, 10);
		v.EraseRange(2, 3);
		VHAL_CHECK_EQ(Counted::live, 7);
		v.PopBack();
		VHAL_CHECK_EQ(Counted::live, 6);
		v.Resize(2);
		VHAL_CHECK_EQ(Counted::live, 2);
		v.Resize(5);
		VHAL_CHECK_EQ(Counted::live, 5);

		Memory::Vector<Counted> copy(v);
		VHAL_CHECK_EQ(Counted::live, 10);
		v.Clear();
		VHAL_CHECK_EQ(Counted::live, 5);
	}
	VHAL_CHECK_EQ(Counted::live, 0);
}


// PushBack takes a reference to an element of the vector while the vector is full. The element is copied before the buffer grows.
void VectorPushBackOfOwnElementWhenFull() {
	MallocAllocator heap;
	Memory::Vector<int> v(&heap);
	VHAL_CHECK(v.PushBack(1));
	VHAL_CHECK(v.PushBack(2));
	VHAL_CHECK(v.PushBack(3));
	VHAL_CHECK(v.PushBack(4));
	VHAL_CHECK_EQ(v.Size(), static_cast<std::size_t>(4));
	v.PushBack(v[0]);
	VHAL_CHECK_EQ(v.Size(), static_cast<std::size_t>(5));
	VHAL_CHECK_EQ(v[4], 1);
}


int main() {
	VectorPushPopIndexAndSize();
	VectorInsertEraseEraseRangeMatchesModel();
	VectorCopyAndMoveSemantics();
	VectorElementLifetimeMatchesSize();
	VectorPushBackOfOwnElementWhenFull();

	return VhalCheck::Finish("TestVectorEdit");
}
