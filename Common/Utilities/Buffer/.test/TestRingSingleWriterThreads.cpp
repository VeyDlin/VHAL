#include <VHAL.h>
#include <VhalCheck.h>
#include <Utilities/Buffer/RingBuffer.h>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <thread>

// One producer thread and one consumer thread share a singleWriter ring of 64 elements. The producer sends a million numbers in order
// and the consumer checks that each arrives once and in order. The consumer takes every other element with Pop() and the others with
// Front() and Drop(1), so both paths run under the threads. A wait that makes no progress for 30 seconds fails the test, so a broken
// ring stops the test and does not hang it.


namespace {


using TestRing = RingBuffer<uint32, 64, Memory::Inside, RingAccess::singleWriter>;


constexpr uint32 elementCount = 1000000;
constexpr std::chrono::seconds waitLimit(30);


void Produce(TestRing& ring, std::atomic<bool>& failed) {
	uint32 value = 0;
	std::chrono::steady_clock::time_point lastProgress = std::chrono::steady_clock::now();

	while (value < elementCount) {
		if (ring.Push(value) == ResultStatus::ok) {
			value++;
			lastProgress = std::chrono::steady_clock::now();
		} else if (std::chrono::steady_clock::now() - lastProgress > waitLimit) {
			failed.store(true);
			return;
		} else {
			std::this_thread::yield();
		}
	}
}


void Consume(TestRing& ring, std::atomic<bool>& failed, std::atomic<uint32>& consumed) {
	uint32 expected = 0;
	std::chrono::steady_clock::time_point lastProgress = std::chrono::steady_clock::now();

	while (expected < elementCount) {
		uint32 value = 0;
		bool got = false;

		if ((expected & 1) == 0) {
			Result<uint32> popped = ring.Pop();
			if (popped.IsOk()) {
				value = popped.Value();
				got = true;
			}
		} else {
			const uint32* front = ring.Front();
			if (front != nullptr) {
				value = *front;
				got = ring.Drop(1) == 1;
			}
		}

		if (got) {
			if (value != expected) {
				failed.store(true);
				return;
			}
			expected++;
			consumed.store(expected);
			lastProgress = std::chrono::steady_clock::now();
		} else if (std::chrono::steady_clock::now() - lastProgress > waitLimit) {
			failed.store(true);
			return;
		} else {
			std::this_thread::yield();
		}
	}
}


}


int main() {
	TestRing ring;
	std::atomic<bool> failed(false);
	std::atomic<uint32> consumed(0);

	std::thread producer(Produce, std::ref(ring), std::ref(failed));
	std::thread consumer(Consume, std::ref(ring), std::ref(failed), std::ref(consumed));
	producer.join();
	consumer.join();

	VHAL_CHECK(!failed.load());
	VHAL_CHECK_EQ(consumed.load(), elementCount);
	VHAL_CHECK(ring.IsEmpty());

	return VhalCheck::Finish("TestRingSingleWriterThreads");
}
