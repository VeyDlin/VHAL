#include <VhalCheck.h>
#include <Utilities/Memory/Heap.h>
#include <Utilities/Memory/Vector.h>

#include "AllocatorDoubles.h"
#include "SimCheck.h"

#include <cstddef>
#include <cstdint>


// Host tests of a vector whose allocator refuses memory: each growing operation reports the refusal, and the vector keeps its
// elements and capacity. The refusing allocator is RefusingAllocator, over a heap of a fixed buffer.


namespace {

// Four elements in a vector of capacity four: the next growing operation needs an allocation.
void FillFour(Memory::Vector<int>& v) {
	for (int i = 1; i <= 4; ++i) {
		v.PushBack(i);
	}
}


bool HoldsOneToFour(const Memory::Vector<int>& v) {
	if (v.Size() != 4) {
		return false;
	}
	for (std::size_t i = 0; i < 4; ++i) {
		if (v[i] != static_cast<int>(i + 1)) {
			return false;
		}
	}
	return true;
}


// Counts the live objects of its type, so a test can see when the vector constructs and destroys them.
struct Live {
	static inline int count = 0;
	int value;

	explicit Live(int v) : value(v) {
		++count;
	}

	Live(const Live& other) : value(other.value) {
		++count;
	}

	Live(Live&& other) noexcept : value(other.value) {
		++count;
	}

	~Live() {
		--count;
	}
};

}


void VectorPushBackWhenGrowFailsKeepsContents() {
	std::uint8_t buffer[4096];
	Memory::Heap<> heap(buffer, sizeof(buffer));
	Memory::AllocatorOf<Memory::Heap<>> inner(heap);
	RefusingAllocator alloc(inner);
	Memory::Vector<int> v(&alloc);
	FillFour(v);
	VHAL_CHECK_EQ(v.Capacity(), static_cast<std::size_t>(4));

	alloc.refusing = true;
	v.PushBack(5);

	VHAL_CHECK(HoldsOneToFour(v));
	VHAL_CHECK_EQ(v.Capacity(), static_cast<std::size_t>(4));

	alloc.refusing = false;
	v.PushBack(5);
	VHAL_CHECK_EQ(v.Size(), static_cast<std::size_t>(5));
	VHAL_CHECK_EQ(v[4], 5);
}


void VectorPushBackCopyWhenGrowFailsKeepsContents() {
	std::uint8_t buffer[4096];
	Memory::Heap<> heap(buffer, sizeof(buffer));
	Memory::AllocatorOf<Memory::Heap<>> inner(heap);
	RefusingAllocator alloc(inner);
	Memory::Vector<int> v(&alloc);
	FillFour(v);

	alloc.refusing = true;
	const int value = 5;
	v.PushBack(value);

	VHAL_CHECK(HoldsOneToFour(v));
}


void VectorEmplaceBackWhenGrowFailsKeepsContents() {
	std::uint8_t buffer[4096];
	Memory::Heap<> heap(buffer, sizeof(buffer));
	Memory::AllocatorOf<Memory::Heap<>> inner(heap);
	RefusingAllocator alloc(inner);
	Memory::Vector<int> v(&alloc);
	FillFour(v);

	alloc.refusing = true;
	v.EmplaceBack(5);

	VHAL_CHECK(HoldsOneToFour(v));
	VHAL_CHECK_EQ(v.Capacity(), static_cast<std::size_t>(4));
}


void VectorEmplaceBackWhenGrowFailsConstructsNothing() {
	std::uint8_t buffer[4096];
	Memory::Heap<> heap(buffer, sizeof(buffer));
	Memory::AllocatorOf<Memory::Heap<>> inner(heap);
	RefusingAllocator alloc(inner);
	Memory::Vector<Live> v(&alloc);
	for (int i = 1; i <= 4; ++i) {
		v.PushBack(Live(i));
	}
	VHAL_CHECK_EQ(Live::count, 4);

	alloc.refusing = true;
	v.EmplaceBack(7);

	VHAL_CHECK_EQ(v.Size(), static_cast<std::size_t>(4));
	VHAL_CHECK_EQ(Live::count, 4);
}


void VectorInsertWhenGrowFailsKeepsContents() {
	std::uint8_t buffer[4096];
	Memory::Heap<> heap(buffer, sizeof(buffer));
	Memory::AllocatorOf<Memory::Heap<>> inner(heap);
	RefusingAllocator alloc(inner);
	Memory::Vector<int> v(&alloc);
	FillFour(v);

	alloc.refusing = true;
	v.Insert(0, 9);

	VHAL_CHECK(HoldsOneToFour(v));
}


void VectorReserveWhenAllocationFailsKeepsContents() {
	std::uint8_t buffer[4096];
	Memory::Heap<> heap(buffer, sizeof(buffer));
	Memory::AllocatorOf<Memory::Heap<>> inner(heap);
	RefusingAllocator alloc(inner);
	Memory::Vector<int> v(&alloc);
	FillFour(v);

	alloc.refusing = true;
	v.Reserve(100);

	VHAL_CHECK(HoldsOneToFour(v));
	VHAL_CHECK_EQ(v.Capacity(), static_cast<std::size_t>(4));
}


void VectorResizeUpWhenAllocationFailsKeepsContents() {
	std::uint8_t buffer[4096];
	Memory::Heap<> heap(buffer, sizeof(buffer));
	Memory::AllocatorOf<Memory::Heap<>> inner(heap);
	RefusingAllocator alloc(inner);
	Memory::Vector<int> v(&alloc);
	FillFour(v);

	alloc.refusing = true;
	v.Resize(10);

	VHAL_CHECK(HoldsOneToFour(v));
	VHAL_CHECK_EQ(v.Capacity(), static_cast<std::size_t>(4));
}


void VectorPushAfterFailedGrowRecovers() {
	std::uint8_t buffer[4096];
	Memory::Heap<> heap(buffer, sizeof(buffer));
	Memory::AllocatorOf<Memory::Heap<>> inner(heap);
	RefusingAllocator alloc(inner);
	Memory::Vector<char> v(&alloc);
	v.PushBack('a');
	v.PushBack('b');
	v.PushBack('c');
	v.PushBack('d');

	alloc.refusing = true;
	v.PushBack('e');
	VHAL_CHECK_EQ(v.Size(), static_cast<std::size_t>(4));

	alloc.refusing = false;
	v.PushBack('e');
	VHAL_CHECK_EQ(v.Size(), static_cast<std::size_t>(5));
	VHAL_CHECK_EQ(v[4], 'e');
}


void VectorGrowingOperationsReportSuccessAndFailure() {
	std::uint8_t buffer[4096];
	Memory::Heap<> heap(buffer, sizeof(buffer));
	Memory::AllocatorOf<Memory::Heap<>> inner(heap);
	RefusingAllocator alloc(inner);
	Memory::Vector<int> v(&alloc);
	VHAL_CHECK(v.PushBack(1));
	VHAL_CHECK(v.PushBack(2));
	VHAL_CHECK(v.PushBack(3));
	VHAL_CHECK(v.PushBack(4));

	alloc.refusing = true;
	VHAL_CHECK(!v.PushBack(5));
	VHAL_CHECK(v.EmplaceBack(5) == nullptr);
	VHAL_CHECK(!v.Insert(0, 9));
	VHAL_CHECK(!v.Reserve(100));
	VHAL_CHECK(!v.Resize(100));
	VHAL_CHECK(v.Reserve(v.Capacity()));
	VHAL_CHECK(v.Resize(v.Size()));
	VHAL_CHECK_EQ(v.Size(), static_cast<std::size_t>(4));

	alloc.refusing = false;
	VHAL_CHECK(v.EmplaceBack(5) != nullptr);
	VHAL_CHECK(v.Reserve(100));
	VHAL_CHECK(v.Resize(7));
	VHAL_CHECK_EQ(v.Size(), static_cast<std::size_t>(7));
}


void VectorCopyAssignWhenAllocationFailsKeepsOldContents() {
	std::uint8_t buffer[4096];
	Memory::Heap<> heap(buffer, sizeof(buffer));
	Memory::AllocatorOf<Memory::Heap<>> inner(heap);
	RefusingAllocator alloc(inner);
	Memory::Vector<int> source(&alloc);
	FillFour(source);
	Memory::Vector<int> target(&alloc);
	target.PushBack(7);
	target.PushBack(8);

	alloc.refusing = true;
	target = source;

	VHAL_CHECK_EQ(target.Size(), static_cast<std::size_t>(2));
	VHAL_CHECK_EQ(target[0], 7);
	VHAL_CHECK_EQ(target[1], 8);
}


void VectorCopyConstructWhenAllocationFailsYieldsEmpty() {
	std::uint8_t buffer[4096];
	Memory::Heap<> heap(buffer, sizeof(buffer));
	Memory::AllocatorOf<Memory::Heap<>> inner(heap);
	RefusingAllocator alloc(inner);
	Memory::Vector<int> source(&alloc);
	FillFour(source);

	alloc.refusing = true;
	Memory::Vector<int> copy(source);

	VHAL_CHECK_EQ(copy.Size(), static_cast<std::size_t>(0));
	VHAL_CHECK_EQ(copy.Capacity(), static_cast<std::size_t>(0));
	VHAL_CHECK(HoldsOneToFour(source));
}


void VectorAssignCopiesAndReportsSuccess() {
	std::uint8_t buffer[4096];
	Memory::Heap<> heap(buffer, sizeof(buffer));
	Memory::AllocatorOf<Memory::Heap<>> inner(heap);
	RefusingAllocator alloc(inner);
	Memory::Vector<int> source(&alloc);
	FillFour(source);
	Memory::Vector<int> target;

	VHAL_CHECK(target.Assign(source));
	VHAL_CHECK(HoldsOneToFour(target));
	VHAL_CHECK(HoldsOneToFour(source));
	VHAL_CHECK(target.Assign(target));
	VHAL_CHECK(HoldsOneToFour(target));
}


void VectorAssignWhenAllocationFailsReportsFalseAndKeepsContents() {
	std::uint8_t buffer[4096];
	Memory::Heap<> heap(buffer, sizeof(buffer));
	Memory::AllocatorOf<Memory::Heap<>> inner(heap);
	RefusingAllocator alloc(inner);
	Memory::Vector<int> source(&alloc);
	FillFour(source);
	Memory::Vector<int> target(&alloc);
	target.PushBack(7);

	alloc.refusing = true;
	VHAL_CHECK(!target.Assign(source));

	VHAL_CHECK_EQ(target.Size(), static_cast<std::size_t>(1));
	VHAL_CHECK_EQ(target[0], 7);
	VHAL_CHECK_EQ(target.Capacity(), static_cast<std::size_t>(4));
}


void VectorAssignEmptySourceEmptiesTarget() {
	std::uint8_t buffer[4096];
	Memory::Heap<> heap(buffer, sizeof(buffer));
	Memory::AllocatorOf<Memory::Heap<>> inner(heap);
	RefusingAllocator alloc(inner);
	Memory::Vector<int> target(&alloc);
	target.PushBack(1);
	Memory::Vector<int> empty(&alloc);

	alloc.refusing = true;
	// Nothing to allocate for an empty source, so even a refusing allocator succeeds.
	VHAL_CHECK(target.Assign(empty));
	VHAL_CHECK_EQ(target.Size(), static_cast<std::size_t>(0));
}


void VectorAssignDestroysOldElementsExactlyOnce() {
	std::uint8_t buffer[4096];
	Memory::Heap<> heap(buffer, sizeof(buffer));
	Memory::AllocatorOf<Memory::Heap<>> inner(heap);
	RefusingAllocator alloc(inner);
	Memory::Vector<Live> source(&alloc);
	for (int i = 1; i <= 3; ++i) {
		source.PushBack(Live(i));
	}
	Memory::Vector<Live> target(&alloc);
	for (int i = 10; i <= 11; ++i) {
		target.PushBack(Live(i));
	}
	VHAL_CHECK_EQ(Live::count, 5);

	VHAL_CHECK(target.Assign(source));
	VHAL_CHECK_EQ(target.Size(), static_cast<std::size_t>(3));
	VHAL_CHECK_EQ(Live::count, 6);
}


// Insert past the end is refused, and an insert at the end is not.
void VectorInsertPastEndReportsFalse() {
	MallocAllocator heap;
	Memory::Vector<int> v(&heap);
	VHAL_CHECK(v.PushBack(1));
	VHAL_CHECK(!v.Insert(2, 9));
	VHAL_CHECK(v.Insert(1, 2));
	VHAL_CHECK_EQ(v.Size(), static_cast<std::size_t>(2));
}


int main() {
	VectorPushBackWhenGrowFailsKeepsContents();
	VectorPushBackCopyWhenGrowFailsKeepsContents();
	VectorEmplaceBackWhenGrowFailsKeepsContents();
	VectorEmplaceBackWhenGrowFailsConstructsNothing();
	VectorInsertWhenGrowFailsKeepsContents();
	VectorReserveWhenAllocationFailsKeepsContents();
	VectorResizeUpWhenAllocationFailsKeepsContents();
	VectorPushAfterFailedGrowRecovers();
	VectorGrowingOperationsReportSuccessAndFailure();
	VectorCopyAssignWhenAllocationFailsKeepsOldContents();
	VectorCopyConstructWhenAllocationFailsYieldsEmpty();
	VectorAssignCopiesAndReportsSuccess();
	VectorAssignWhenAllocationFailsReportsFalseAndKeepsContents();
	VectorAssignEmptySourceEmptiesTarget();
	VectorAssignDestroysOldElementsExactlyOnce();
	VectorInsertPastEndReportsFalse();

	return VhalCheck::Finish("TestVectorRefusal");
}
