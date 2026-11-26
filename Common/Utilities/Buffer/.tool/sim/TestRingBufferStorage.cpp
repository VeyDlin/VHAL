#include <VHAL.h>
#include <Utilities/Buffer/RingBuffer.h>
#include <Utilities/Memory/Heap.h>
#include <Utilities/Memory/Storage.h>
#include <array>
#include <cstddef>
#include <cstdio>
#include <span>

// Host tests of the RingBuffer storage policies: External, FromHeap and the not-ready refusal. The static ring of a second
// translation unit (RingSecondUnit.cpp) is built from the heap defined here.


int RingSecondUnitPushes();


namespace {


int failureCount = 0;


void Expect(bool condition, const char* expression, int line) {
	if (!condition) {
		failureCount++;
		std::fprintf(stderr, "TestRingBufferStorage.cpp:%d: check failed: %s\n", line, expression);
	}
}


void ExpectEqual(long long actual, long long expected, const char* expression, int line) {
	if (actual != expected) {
		failureCount++;
		std::fprintf(stderr, "TestRingBufferStorage.cpp:%d: %s is %lld, expected %lld\n", line, expression, actual, expected);
	}
}


#define RS_EXPECT(condition) Expect(static_cast<bool>(condition), #condition, __LINE__)
#define RS_EQ(actual, expected) ExpectEqual(static_cast<long long>(actual), static_cast<long long>(expected), #actual, __LINE__)


}


alignas(8) std::uint8_t ringTestArena[256];
Memory::Heap<> ringTestHeap(ringTestArena, sizeof(ringTestArena));

alignas(8) std::uint8_t ringTinyArena[64];
Memory::Heap<> ringTinyHeap(ringTinyArena, sizeof(ringTinyArena));


namespace {


void ExternalRing() {
	std::array<uint8, 8> memory = {};
	RingBuffer<uint8, 8, Memory::External> ring{std::span<uint8>(memory)};
	RS_EXPECT(ring.IsEmpty());

	std::array<uint8, 3> three = {1, 2, 3};
	RS_EQ(ring.Push(std::span<const uint8>(three)), 3);
	RS_EQ(ring.Size(), 3);
	RS_EQ(memory[0], 1);
	RS_EQ(memory[2], 3);

	std::array<uint8, 3> back = {};
	RS_EQ(ring.Pop(std::span<uint8>(back)), 3);
	RS_EQ(back[0], 1);
	RS_EQ(back[2], 3);
}


void ExternalTooSmall() {
	std::array<uint8, 4> memory = {};
	RingBuffer<uint8, 8, Memory::External> ring{std::span<uint8>(memory)};

	std::array<uint8, 2> two = {9, 9};
	RS_EQ(ring.Push(std::span<const uint8>(two)), 0);
	RS_EQ(ring.Size(), 0);
	RS_EQ(ring.Push(static_cast<uint8>(5)), static_cast<int>(ResultStatus::notReady));
	RS_EQ(ring.Size(), 0);
}


void FromHeapRing() {
	std::size_t usedBefore = ringTestHeap.UsedBytes();
	{
		RingBuffer<uint8, 16, Memory::FromHeap<ringTestHeap>> ring;
		RS_EXPECT(ringTestHeap.UsedBytes() > usedBefore);

		std::array<uint8, 10> ten = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
		RS_EQ(ring.Push(std::span<const uint8>(ten)), 10);

		std::array<uint8, 10> back = {};
		RS_EQ(ring.Pop(std::span<uint8>(back)), 10);
		RS_EQ(back[0], 1);
		RS_EQ(back[9], 10);
		RS_EXPECT(ring.IsEmpty());
	}
	RS_EQ(ringTestHeap.UsedBytes(), usedBefore);
}


void FromHeapExhausted() {
	RingBuffer<uint8, 4096, Memory::FromHeap<ringTinyHeap>> ring;
	RS_EQ(ring.Push(static_cast<uint8>(1)) == ResultStatus::notReady, 1);
	std::array<uint8, 1> one = {1};
	RS_EQ(ring.Push(std::span<const uint8>(one)), 0);
	RS_EQ(ring.Size(), 0);
}


void SecondUnit() {
	RS_EQ(RingSecondUnitPushes(), 3);
}


}


int main() {
	ExternalRing();
	ExternalTooSmall();
	FromHeapRing();
	FromHeapExhausted();
	SecondUnit();

	if (SimCriticalDepth() != 0) {
		failureCount++;
		std::fprintf(stderr, "a critical section is still open\n");
	}

	if (failureCount == 0) {
		std::printf("TestRingBufferStorage: all checks passed\n");
		return 0;
	}

	std::printf("TestRingBufferStorage: %d check(s) failed\n", failureCount);
	return 1;
}
