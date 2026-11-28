#include <VHAL.h>
#include <VhalCheck.h>
#include <Utilities/Buffer/RingBuffer.h>
#include <Utilities/Memory/Storage.h>
#include <cstddef>
#include <span>
#include <utility>

// Host tests of the access modes of RingBuffer, member by member: the members each mode offers, the critical sections each mode takes,
// capacity N in every mode, Memory::External with N + 1 elements, and the move of a ring in every mode. The shim counts the critical
// sections, so a call that must take none is checked against the count before and after it.


namespace {


// Runs one call and returns how many critical sections it took.
template <typename Call>
uint32 SectionsTaken(Call&& call) {
	uint32 before = simCriticalEnters;
	call();
	return simCriticalEnters - before;
}


template <RingAccess Access>
void LookupsInEveryMode() {
	RingBuffer<uint8, 3, Memory::Inside, Access> ring;
	VHAL_CHECK(ring.Front() == nullptr);
	VHAL_CHECK(ring.Peek(0) == nullptr);

	VHAL_CHECK_EQ(ring.Push(static_cast<uint8>(10)), ResultStatus::ok);
	VHAL_CHECK_EQ(ring.Push(static_cast<uint8>(20)), ResultStatus::ok);
	VHAL_CHECK_EQ(ring.Push(static_cast<uint8>(30)), ResultStatus::ok);

	const uint8* front = ring.Front();
	VHAL_CHECK(front != nullptr);
	VHAL_CHECK_EQ(*front, static_cast<uint8>(10));
	VHAL_CHECK_EQ(*ring.Peek(0), static_cast<uint8>(10));
	VHAL_CHECK_EQ(*ring.Peek(1), static_cast<uint8>(20));
	VHAL_CHECK_EQ(*ring.Peek(2), static_cast<uint8>(30));
	VHAL_CHECK(ring.Peek(3) == nullptr);

	// Pop the oldest, then wrap: the lookups follow the tail.
	VHAL_CHECK_EQ(ring.Pop().Value(), static_cast<uint8>(10));
	VHAL_CHECK_EQ(ring.Push(static_cast<uint8>(40)), ResultStatus::ok);
	VHAL_CHECK_EQ(*ring.Front(), static_cast<uint8>(20));
	VHAL_CHECK_EQ(*ring.Peek(1), static_cast<uint8>(30));
	VHAL_CHECK_EQ(*ring.Peek(2), static_cast<uint8>(40));
	VHAL_CHECK(ring.Peek(3) == nullptr);
	VHAL_CHECK_EQ(ring[2], static_cast<uint8>(40));
}


template <RingAccess Access>
void DropInEveryMode() {
	RingBuffer<uint8, 4, Memory::Inside, Access> ring;
	for (uint8 value = 1; value <= 4; value++) {
		VHAL_CHECK_EQ(ring.Push(value), ResultStatus::ok);
	}

	VHAL_CHECK_EQ(ring.Drop(2), static_cast<uint16>(2));
	VHAL_CHECK_EQ(*ring.Front(), static_cast<uint8>(3));
	VHAL_CHECK_EQ(ring.Size(), static_cast<uint16>(2));

	// Marked reads refuse a drop, as they refuse a pop.
	VHAL_CHECK_EQ(ring.MarkRead(1), ResultStatus::ok);
	VHAL_CHECK_EQ(ring.Drop(1), static_cast<uint16>(0));
	ring.RollbackReads();

	VHAL_CHECK_EQ(ring.Drop(10), static_cast<uint16>(2));
	VHAL_CHECK(ring.IsEmpty());
	VHAL_CHECK_EQ(ring.Drop(1), static_cast<uint16>(0));
}


template <RingAccess Access>
void CapacityIsNInEveryMode() {
	RingBuffer<uint8, 5, Memory::Inside, Access> ring;
	VHAL_CHECK_EQ(ring.MaxSize(), static_cast<uint16>(5));
	VHAL_CHECK_EQ(ring.GetFreeSpace(), static_cast<uint16>(5));

	const uint8 values[7] = {1, 2, 3, 4, 5, 6, 7};
	VHAL_CHECK_EQ(ring.Push(std::span<const uint8>(values, 7)), static_cast<uint16>(5));
	VHAL_CHECK(ring.IsFull());
	VHAL_CHECK_EQ(ring.Push(static_cast<uint8>(9)), ResultStatus::filled);
	VHAL_CHECK_EQ(ring.GetFreeSpace(), static_cast<uint16>(0));

	uint8 out[8] = {};
	VHAL_CHECK_EQ(ring.Pop(std::span<uint8>(out, 8)), static_cast<uint16>(5));
	VHAL_CHECK_EQ(out[0], static_cast<uint8>(1));
	VHAL_CHECK_EQ(out[4], static_cast<uint8>(5));
	VHAL_CHECK(ring.IsEmpty());
	VHAL_CHECK_EQ(ring.GetFreeSpace(), static_cast<uint16>(5));
}


template <RingAccess Access>
void MoveLeavesSourceEmpty() {
	RingBuffer<uint8, 4, Memory::Inside, Access> source;
	VHAL_CHECK_EQ(source.Push(static_cast<uint8>(7)), ResultStatus::ok);
	VHAL_CHECK_EQ(source.Push(static_cast<uint8>(8)), ResultStatus::ok);
	VHAL_CHECK_EQ(source.MarkRead(1), ResultStatus::ok);

	RingBuffer<uint8, 4, Memory::Inside, Access> target(std::move(source));
	VHAL_CHECK_EQ(target.Size(), static_cast<uint16>(2));
	VHAL_CHECK_EQ(target.GetUncommittedCount(), static_cast<uint16>(1));
	VHAL_CHECK_EQ(*target.Front(), static_cast<uint8>(7));
	VHAL_CHECK(source.IsEmpty());
	VHAL_CHECK_EQ(source.GetUncommittedCount(), static_cast<uint16>(0));
	VHAL_CHECK_EQ(source.Push(static_cast<uint8>(1)), ResultStatus::ok);
	VHAL_CHECK_EQ(*source.Front(), static_cast<uint8>(1));
}


void LockedTakesOneSectionPerAccess() {
	RingBuffer<uint8, 4> ring;
	VHAL_CHECK_EQ(SectionsTaken([&] { ring.Push(static_cast<uint8>(1)); }), 1u);
	VHAL_CHECK_EQ(SectionsTaken([&] { (void)ring.Front(); }), 1u);
	VHAL_CHECK_EQ(SectionsTaken([&] { (void)ring.Peek(0); }), 1u);
	VHAL_CHECK_EQ(SectionsTaken([&] { (void)ring.Drop(1); }), 1u);
	VHAL_CHECK_EQ(SectionsTaken([&] { ring.Push(static_cast<uint8>(2)); }), 1u);
	VHAL_CHECK_EQ(SectionsTaken([&] { (void)ring.Pop(); }), 1u);
	VHAL_CHECK_EQ(SectionsTaken([&] { ring.Clear(); }), 1u);
}


template <RingAccess Access>
void NonLockedModesTakeNoSection() {
	RingBuffer<uint8, 4, Memory::Inside, Access> ring;
	VHAL_CHECK_EQ(SectionsTaken([&] { ring.Push(static_cast<uint8>(1)); }), 0u);
	VHAL_CHECK_EQ(SectionsTaken([&] { ring.Push(static_cast<uint8>(2)); }), 0u);
	VHAL_CHECK_EQ(SectionsTaken([&] { (void)ring.Front(); }), 0u);
	VHAL_CHECK_EQ(SectionsTaken([&] { (void)ring.Peek(1); }), 0u);
	VHAL_CHECK_EQ(SectionsTaken([&] { (void)ring.Pop(); }), 0u);
	VHAL_CHECK_EQ(SectionsTaken([&] { (void)ring.Drop(1); }), 0u);
	VHAL_CHECK_EQ(SectionsTaken([&] { (void)ring.Size(); }), 0u);
	VHAL_CHECK_EQ(SectionsTaken([&] { (void)ring.IsFull(); }), 0u);
	VHAL_CHECK_EQ(SectionsTaken([&] { (void)ring.GetFreeSpace(); }), 0u);
}


template <RingAccess Access>
void ExternalNeedsNPlusOneInNonLockedModes() {
	constexpr uint16 capacity = 4;
	uint8 memory[capacity + 1] = {};

	RingBuffer<uint8, capacity, Memory::External, Access> ring(std::span<uint8>(memory, capacity));
	VHAL_CHECK_EQ(ring.Push(static_cast<uint8>(1)), ResultStatus::notReady);

	RingBuffer<uint8, capacity, Memory::External, Access> enough(std::span<uint8>(memory, capacity + 1));
	uint16 pushed = 0;
	for (uint8 value = 1; value <= capacity; value++) {
		if (enough.Push(value) == ResultStatus::ok) {
			pushed++;
		}
	}
	VHAL_CHECK_EQ(pushed, static_cast<uint16>(capacity));
	VHAL_CHECK_EQ(*enough.Front(), static_cast<uint8>(1));
	VHAL_CHECK_EQ(*enough.Peek(3), static_cast<uint8>(4));
}


void ExternalLockedNeedsOnlyN() {
	constexpr uint16 capacity = 4;
	uint8 memory[capacity] = {};

	RingBuffer<uint8, capacity, Memory::External, RingAccess::locked> ring(std::span<uint8>(memory, capacity));
	VHAL_CHECK_EQ(ring.Push(static_cast<uint8>(9)), ResultStatus::ok);
	VHAL_CHECK_EQ(*ring.Front(), static_cast<uint8>(9));
}


// The index arithmetic at the largest capacity of an uint16 index: the ring holds 65534 elements in one slot less than 65535.
void LargestCapacityWraps() {
	static RingBuffer<uint8, 65534, Memory::Inside, RingAccess::singleWriter> largest;
	uint32 sum = 0;
	for (uint32 round = 0; round < 3; round++) {
		uint32 value = 0;
		for (uint32 index = 0; index < 65534; index++) {
			VHAL_CHECK_EQ(largest.Push(static_cast<uint8>(value)), ResultStatus::ok);
			value++;
		}
		VHAL_CHECK(largest.IsFull());
		VHAL_CHECK_EQ(largest.Push(static_cast<uint8>(0)), ResultStatus::filled);
		value = 0;
		for (uint32 index = 0; index < 65534; index++) {
			Result<uint8> popped = largest.Pop();
			VHAL_CHECK(popped.IsOk());
			VHAL_CHECK_EQ(popped.Value(), static_cast<uint8>(value));
			sum += popped.Value();
			value++;
		}
		VHAL_CHECK(largest.IsEmpty());
	}
	VHAL_CHECK(sum > 0);
}


}


int main() {
	LookupsInEveryMode<RingAccess::locked>();
	LookupsInEveryMode<RingAccess::singleWriter>();
	LookupsInEveryMode<RingAccess::callerLocked>();

	DropInEveryMode<RingAccess::locked>();
	DropInEveryMode<RingAccess::singleWriter>();
	DropInEveryMode<RingAccess::callerLocked>();

	CapacityIsNInEveryMode<RingAccess::locked>();
	CapacityIsNInEveryMode<RingAccess::singleWriter>();
	CapacityIsNInEveryMode<RingAccess::callerLocked>();

	MoveLeavesSourceEmpty<RingAccess::locked>();
	MoveLeavesSourceEmpty<RingAccess::singleWriter>();
	MoveLeavesSourceEmpty<RingAccess::callerLocked>();

	LockedTakesOneSectionPerAccess();
	NonLockedModesTakeNoSection<RingAccess::singleWriter>();
	NonLockedModesTakeNoSection<RingAccess::callerLocked>();

	ExternalNeedsNPlusOneInNonLockedModes<RingAccess::singleWriter>();
	ExternalNeedsNPlusOneInNonLockedModes<RingAccess::callerLocked>();
	ExternalLockedNeedsOnlyN();

	LargestCapacityWraps();

	return VhalCheck::Finish("TestRingModes");
}
