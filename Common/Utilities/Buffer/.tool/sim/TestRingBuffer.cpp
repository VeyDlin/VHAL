#include <VHAL.h>
#include <Utilities/Buffer/RingBuffer.h>
#include <array>
#include <cstddef>
#include <cstdio>
#include <deque>
#include <span>

// Host tests for the bulk Push and Pop of RingBuffer, and for the marked-read rules of the whole class. The shim counts critical
// sections, so each case also checks how many sections a call takes: a bulk call takes one, however many elements it moves.
// A random case runs the ring against a plain deque with a fixed seed.


namespace {


int failureCount = 0;


void Expect(bool condition, const char* expression, int line) {
	if (!condition) {
		failureCount++;
		std::fprintf(stderr, "TestRingBuffer.cpp:%d: check failed: %s\n", line, expression);
	}
}


void ExpectEqual(long long actual, long long expected, const char* expression, int line) {
	if (actual != expected) {
		failureCount++;
		std::fprintf(stderr, "TestRingBuffer.cpp:%d: %s is %lld, expected %lld\n", line, expression, actual, expected);
	}
}


#define RB_EXPECT(condition) Expect(static_cast<bool>(condition), #condition, __LINE__)
#define RB_EQ(actual, expected) ExpectEqual(static_cast<long long>(actual), static_cast<long long>(expected), #actual, __LINE__)


bool SameBytes(std::span<const uint8> actual, std::span<const uint8> expected) {
	if (actual.size() != expected.size()) {
		return false;
	}

	for (std::size_t index = 0; index < actual.size(); index++) {
		if (actual[index] != expected[index]) {
			return false;
		}
	}

	return true;
}


// Sections the calls between the two marks took: the shim counts every enter.
uint32 SectionsSince(uint32 mark) {
	return simCriticalEnters - mark;
}


// The ring is small, so the wrap-around and the limits come up after a few calls.
using Ring8 = RingBuffer<uint8, 8>;


void TestBulkRoundTrip() {
	Ring8 ring;
	std::array<uint8, 5> input = { 1, 2, 3, 4, 5 };
	std::array<uint8, 5> output{};

	uint32 mark = simCriticalEnters;
	RB_EQ(ring.Push(std::span<const uint8>(input)), 5);
	RB_EQ(SectionsSince(mark), 1);

	RB_EQ(ring.Size(), 5);
	RB_EQ(ring.GetFreeSpace(), 3);

	mark = simCriticalEnters;
	RB_EQ(ring.Pop(std::span<uint8>(output)), 5);
	RB_EQ(SectionsSince(mark), 1);

	RB_EXPECT(SameBytes(output, input));
	RB_EQ(ring.Size(), 0);
	RB_EXPECT(ring.IsEmpty());
}


// The head sits at index 6, so five bytes are written at 6, 7, 0, 1, 2 and read back the same way.
void TestWrapAround() {
	Ring8 ring;
	std::array<uint8, 6> six = { 10, 11, 12, 13, 14, 15 };
	std::array<uint8, 6> drained{};
	RB_EQ(ring.Push(std::span<const uint8>(six)), 6);
	RB_EQ(ring.Pop(std::span<uint8>(drained)), 6);

	std::array<uint8, 5> wrapIn = { 20, 21, 22, 23, 24 };
	RB_EQ(ring.Push(std::span<const uint8>(wrapIn)), 5);
	RB_EQ(ring.Size(), 5);

	std::array<uint8, 5> wrapOut{};
	RB_EQ(ring.Pop(std::span<uint8>(wrapOut)), 5);
	RB_EXPECT(SameBytes(wrapOut, wrapIn));
	RB_EXPECT(ring.IsEmpty());

	// A push that fills across the end, then a pop that starts past the end and wraps again.
	std::array<uint8, 8> fill = { 1, 2, 3, 4, 5, 6, 7, 8 };
	RB_EQ(ring.Push(std::span<const uint8>(fill)), 8);
	std::array<uint8, 3> first{};
	RB_EQ(ring.Pop(std::span<uint8>(first)), 3);
	RB_EXPECT(SameBytes(first, std::span<const uint8>(fill).first(3)));

	std::array<uint8, 3> again = { 9, 10, 11 };
	RB_EQ(ring.Push(std::span<const uint8>(again)), 3);

	std::array<uint8, 8> rest{};
	RB_EQ(ring.Pop(std::span<uint8>(rest)), 8);
	std::array<uint8, 8> expectedRest = { 4, 5, 6, 7, 8, 9, 10, 11 };
	RB_EXPECT(SameBytes(rest, expectedRest));
}


void TestFull() {
	Ring8 ring;
	std::array<uint8, 12> twelve{};
	for (std::size_t index = 0; index < twelve.size(); index++) {
		twelve[index] = static_cast<uint8>(index);
	}

	RB_EQ(ring.Push(std::span<const uint8>(twelve)), 8);
	RB_EXPECT(ring.IsFull());
	RB_EQ(ring.GetFreeSpace(), 0);

	uint32 mark = simCriticalEnters;
	RB_EQ(ring.Push(std::span<const uint8>(twelve)), 0);
	RB_EQ(SectionsSince(mark), 1);
	RB_EQ(ring.Size(), 8);

	std::array<uint8, 8> out{};
	RB_EQ(ring.Pop(std::span<uint8>(out)), 8);
	std::array<uint8, 8> expected = { 0, 1, 2, 3, 4, 5, 6, 7 };
	RB_EXPECT(SameBytes(out, expected));
}


void TestEmpty() {
	Ring8 ring;
	std::array<uint8, 4> out = { 9, 9, 9, 9 };

	uint32 mark = simCriticalEnters;
	RB_EQ(ring.Pop(std::span<uint8>(out)), 0);
	RB_EQ(SectionsSince(mark), 1);
	RB_EQ(out[0], 9);
	RB_EQ(out[3], 9);

	RB_EQ(ring.Push(std::span<const uint8>()), 0);
	RB_EQ(ring.Pop(std::span<uint8>()), 0);
	RB_EXPECT(ring.IsEmpty());
}


void TestPartialPushAndPop() {
	Ring8 ring;
	std::array<uint8, 3> three = { 1, 2, 3 };
	std::array<uint8, 7> seven = { 100, 101, 102, 103, 104, 105, 106 };
	RB_EQ(ring.Push(std::span<const uint8>(three)), 3);

	// Five of the seven fit: the copy stops at the free room and returns it.
	RB_EQ(ring.Push(std::span<const uint8>(seven)), 5);
	RB_EQ(ring.Size(), 8);

	std::array<uint8, 2> two{};
	RB_EQ(ring.Pop(std::span<uint8>(two)), 2);
	RB_EXPECT(SameBytes(two, std::span<const uint8>(three).first(2)));

	std::array<uint8, 10> ten{};
	RB_EQ(ring.Pop(std::span<uint8>(ten)), 6);
	std::array<uint8, 6> expected = { 3, 100, 101, 102, 103, 104 };
	RB_EXPECT(SameBytes(std::span<const uint8>(ten).first(6), expected));
	RB_EXPECT(ring.IsEmpty());
}


// Marked elements stay in place: a bulk Pop refuses with 0 until they are committed or rolled back, and a bulk Push still
// adds elements into free room, but cannot use the room the marks hold.
void TestMarkedReads() {
	Ring8 ring;
	std::array<uint8, 6> six = { 1, 2, 3, 4, 5, 6 };
	RB_EQ(ring.Push(std::span<const uint8>(six)), 6);
	RB_EXPECT(ring.MarkRead(3) == ResultStatus::ok);
	RB_EQ(ring.GetUncommittedCount(), 3);

	std::array<uint8, 4> out = { 77, 77, 77, 77 };
	uint32 mark = simCriticalEnters;
	RB_EQ(ring.Pop(std::span<uint8>(out)), 0);
	RB_EQ(SectionsSince(mark), 1);
	RB_EQ(out[0], 77);
	RB_EQ(ring.Size(), 6);

	std::array<uint8, 2> fifty = { 50, 51 };
	RB_EQ(ring.Push(std::span<const uint8>(fifty)), 2);
	RB_EQ(ring.Size(), 8);
	RB_EQ(ring.Push(std::span<const uint8>(fifty)), 0);

	// Committing removes the three marked bytes, which were the oldest.
	ring.CommitReads();
	RB_EQ(ring.GetUncommittedCount(), 0);
	RB_EQ(ring.Size(), 5);

	std::array<uint8, 5> committed{};
	RB_EQ(ring.Pop(std::span<uint8>(committed)), 5);
	std::array<uint8, 5> expectedCommitted = { 4, 5, 6, 50, 51 };
	RB_EXPECT(SameBytes(committed, expectedCommitted));

	// Rolling back puts the marked bytes back in reach of the bulk Pop.
	std::array<uint8, 4> four = { 1, 2, 3, 4 };
	RB_EQ(ring.Push(std::span<const uint8>(four)), 4);
	RB_EXPECT(ring.MarkRead(2) == ResultStatus::ok);
	RB_EQ(ring.Pop(std::span<uint8>(out)), 0);
	ring.RollbackReads();

	std::array<uint8, 4> rolledBack{};
	RB_EQ(ring.Pop(std::span<uint8>(rolledBack)), 4);
	RB_EXPECT(SameBytes(rolledBack, four));

	// A mark cannot exceed the elements that are not yet marked.
	RB_EQ(ring.Push(std::span<const uint8>(four)), 4);
	RB_EXPECT(ring.MarkRead(5) == ResultStatus::error);
	RB_EQ(ring.GetUncommittedCount(), 0);
	ring.Clear();
}


// The single-element Push and Pop keep their behaviour, and one bulk call is one section where eight single calls are eight.
void TestSectionCount() {
	Ring8 ring;
	std::array<uint8, 8> eight = { 1, 2, 3, 4, 5, 6, 7, 8 };

	uint32 mark = simCriticalEnters;
	for (uint8 value : eight) {
		RB_EXPECT(ring.Push(value) == ResultStatus::ok);
	}
	RB_EQ(SectionsSince(mark), 8);

	ring.Clear();
	mark = simCriticalEnters;
	RB_EQ(ring.Push(std::span<const uint8>(eight)), 8);
	RB_EQ(SectionsSince(mark), 1);
}


// Random operations against a deque. The ring has 13 elements, so the index arithmetic does not line up with a power of two.
uint32 NextRandom(uint32& state) {
	state = state * 1103515245u + 12345u;
	return (state >> 16) & 0x7FFFu;
}


void TestRandomAgainstModel() {
	constexpr uint16 Capacity = 13;
	RingBuffer<uint8, Capacity> ring;
	std::deque<uint8> model;
	uint16 marked = 0;
	uint32 state = 12345;
	std::array<uint8, 20> scratch{};

	for (uint32 step = 0; step < 4000; step++) {
		uint32 operation = NextRandom(state) % 9;

		if (operation == 0) {
			uint16 length = static_cast<uint16>(NextRandom(state) % 20);
			std::array<uint8, 20> data{};
			for (uint16 index = 0; index < length; index++) {
				data[index] = static_cast<uint8>(NextRandom(state));
			}

			uint16 room = static_cast<uint16>(Capacity - model.size());
			uint16 expected = length < room ? length : room;
			RB_EQ(ring.Push(std::span<const uint8>(data).first(length)), expected);
			for (uint16 index = 0; index < expected; index++) {
				model.push_back(data[index]);
			}
		} else if (operation == 1) {
			uint16 length = static_cast<uint16>(NextRandom(state) % 20);
			uint16 expected = 0;
			if (marked == 0) {
				size_t available = model.size();
				expected = static_cast<size_t>(length) < available ? length : static_cast<uint16>(available);
			}

			uint16 popped = ring.Pop(std::span<uint8>(scratch).first(length));
			RB_EQ(popped, expected);
			// The model bounds the loop: after a mismatch in popped, front() must not run on an empty deque.
			for (uint16 index = 0; index < popped && !model.empty(); index++) {
				RB_EQ(scratch[index], model.front());
				model.pop_front();
			}
		} else if (operation == 2) {
			uint8 value = static_cast<uint8>(NextRandom(state));
			ResultStatus status = ring.Push(value);
			if (model.size() < static_cast<size_t>(Capacity)) {
				RB_EXPECT(status == ResultStatus::ok);
				model.push_back(value);
			} else {
				RB_EXPECT(status == ResultStatus::filled);
			}
		} else if (operation == 3) {
			uint16 count = static_cast<uint16>(NextRandom(state) % 6);
			ResultStatus status = ring.MarkRead(count);
			if (static_cast<size_t>(count) > model.size() - marked) {
				RB_EXPECT(status == ResultStatus::error);
			} else {
				RB_EXPECT(status == ResultStatus::ok);
				marked = static_cast<uint16>(marked + count);
			}
		} else if (operation == 4) {
			ring.CommitReads();
			for (uint16 index = 0; index < marked && !model.empty(); index++) {
				model.pop_front();
			}

			marked = 0;
		} else if (operation == 5) {
			ring.RollbackReads();
			marked = 0;
		} else if (operation == 6 && NextRandom(state) % 40 == 0) {
			ring.Clear();
			model.clear();
			marked = 0;
		}

		RB_EQ(ring.Size(), model.size());
		RB_EQ(ring.GetUncommittedCount(), marked);
		RB_EQ(ring.IsFull(), model.size() == static_cast<size_t>(Capacity));
		RB_EQ(ring.IsEmpty(), model.empty());
	}
}


}


int main() {
	TestBulkRoundTrip();
	TestWrapAround();
	TestFull();
	TestEmpty();
	TestPartialPushAndPop();
	TestMarkedReads();
	TestSectionCount();
	TestRandomAgainstModel();

	if (SimCriticalDepth() != 0) {
		failureCount++;
		std::fprintf(stderr, "a critical section is still open\n");
	}

	if (failureCount == 0) {
		std::printf("TestRingBuffer: all checks passed\n");
		return 0;
	}

	std::printf("TestRingBuffer: %d check(s) failed\n", failureCount);
	return 1;
}
