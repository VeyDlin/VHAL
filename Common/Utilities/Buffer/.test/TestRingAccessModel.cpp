#include <VHAL.h>
#include <VhalCheck.h>
#include <Utilities/Buffer/RingBuffer.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <random>
#include <span>

// Host tests of RingBuffer in every access mode against a model: seeded random pushes, pops, drops, lookups, marked reads, commits,
// rollbacks and (locked and callerLocked only) clears. The capacities include 1 and 2, where the spare slot of the indexed modes makes
// the wrap happen at every step, and 255, where the indexes wrap many times.


namespace {


template <RingAccess Access, uint16 Capacity>
void RunModel(uint32 seed, uint32 steps) {
	RingBuffer<uint8, Capacity, Memory::Inside, Access> ring;
	std::deque<uint8> held;  // every element of the ring, marked or not
	uint16 marked = 0;
	std::mt19937 random(seed);
	uint8 nextValue = 1;

	for (uint32 step = 0; step < steps; step++) {
		uint32 operation = random() % 12;

		if (operation < 3) {
			ResultStatus status = ring.Push(nextValue);
			if (held.size() < Capacity) {
				VHAL_CHECK_EQ(status, ResultStatus::ok);
				held.push_back(nextValue);
			} else {
				VHAL_CHECK_EQ(status, ResultStatus::filled);
			}
			nextValue++;
		} else if (operation == 3) {
			uint16 count = static_cast<uint16>(random() % 4);
			uint8 batch[3] = {};
			for (uint16 index = 0; index < count; index++) {
				batch[index] = nextValue;
				nextValue++;
			}

			uint16 expected = static_cast<uint16>(std::min<std::size_t>(count, Capacity - held.size()));
			uint16 pushed = ring.Push(std::span<const uint8>(batch, count));
			VHAL_CHECK_EQ(pushed, expected);
			for (uint16 index = 0; index < pushed; index++) {
				held.push_back(batch[index]);
			}
		} else if (operation == 4 || operation == 5) {
			Result<uint8> popped = ring.Pop();
			if (marked > 0) {
				VHAL_CHECK(popped.IsErr());
				VHAL_CHECK_EQ(popped.Error(), ResultStatus::busy);
			} else if (held.empty()) {
				VHAL_CHECK(popped.IsErr());
				VHAL_CHECK_EQ(popped.Error(), ResultStatus::empty);
			} else {
				VHAL_CHECK(popped.IsOk());
				VHAL_CHECK_EQ(popped.Value(), held.front());
				held.pop_front();
			}
		} else if (operation == 6) {
			uint16 want = static_cast<uint16>(random() % 4);
			uint8 out[3] = {};
			uint16 expected = marked > 0 ? 0 : static_cast<uint16>(std::min<std::size_t>(want, held.size()));
			uint16 popped = ring.Pop(std::span<uint8>(out, want));
			VHAL_CHECK_EQ(popped, expected);
			for (uint16 index = 0; index < popped; index++) {
				VHAL_CHECK_EQ(out[index], held.front());
				held.pop_front();
			}
		} else if (operation == 7) {
			uint16 count = static_cast<uint16>(random() % 4);
			uint16 expected = marked > 0 ? 0 : static_cast<uint16>(std::min<std::size_t>(count, held.size()));
			VHAL_CHECK_EQ(ring.Drop(count), expected);
			for (uint16 index = 0; index < expected; index++) {
				held.pop_front();
			}
		} else if (operation == 8) {
			uint16 index = static_cast<uint16>(random() % (Capacity + 1));
			const uint8* item = ring.Peek(index);
			if (index < held.size()) {
				VHAL_CHECK(item != nullptr);
				VHAL_CHECK_EQ(*item, held[index]);
				VHAL_CHECK_EQ(ring[index], held[index]);
			} else {
				VHAL_CHECK(item == nullptr);
			}

			const uint8* front = ring.Front();
			if (held.empty()) {
				VHAL_CHECK(front == nullptr);
			} else {
				VHAL_CHECK(front != nullptr);
				VHAL_CHECK_EQ(*front, held.front());
			}
		} else if (operation == 9) {
			uint16 offset = static_cast<uint16>(random() % (Capacity + 1));
			uint8 buffer[3] = {};
			uint16 expected = 0;
			if (offset < held.size()) {
				expected = static_cast<uint16>(std::min<std::size_t>(3, held.size() - offset));
			}

			uint16 peeked = ring.PeekMultiple(buffer, 3, offset);
			VHAL_CHECK_EQ(peeked, expected);
			for (uint16 index = 0; index < peeked; index++) {
				VHAL_CHECK_EQ(buffer[index], held[offset + index]);
			}
		} else if (operation == 10) {
			uint16 count = static_cast<uint16>(random() % 3);
			ResultStatus status = ring.MarkRead(count);
			if (count <= held.size() - marked) {
				VHAL_CHECK_EQ(status, ResultStatus::ok);
				marked = static_cast<uint16>(marked + count);
			} else {
				VHAL_CHECK_EQ(status, ResultStatus::error);
			}
		} else if (random() % 2 == 0) {
			if (random() % 2 == 0) {
				ring.CommitReads();
				for (uint16 index = 0; index < marked; index++) {
					held.pop_front();
				}
			} else {
				ring.RollbackReads();
			}
			marked = 0;
		} else {
			if constexpr (Access != RingAccess::singleWriter) {
				if (random() % 4 == 0) {
					ring.Clear();
					held.clear();
					marked = 0;
				}
			}
		}

		VHAL_CHECK_EQ(ring.Size(), static_cast<uint16>(held.size()));
		VHAL_CHECK_EQ(ring.IsFull(), held.size() == Capacity);
		VHAL_CHECK_EQ(ring.IsEmpty(), held.empty());
		VHAL_CHECK_EQ(ring.GetFreeSpace(), static_cast<uint16>(Capacity - held.size()));
		VHAL_CHECK_EQ(ring.GetUncommittedCount(), marked);
	}
}


template <RingAccess Access>
void RunModelAtCapacities(uint32 seed) {
	RunModel<Access, 1>(seed, 4000);
	RunModel<Access, 2>(seed + 1, 4000);
	RunModel<Access, 3>(seed + 2, 4000);
	RunModel<Access, 7>(seed + 3, 4000);
	RunModel<Access, 16>(seed + 4, 4000);
	RunModel<Access, 255>(seed + 5, 4000);
}


}


int main() {
	RunModelAtCapacities<RingAccess::locked>(1000);
	RunModelAtCapacities<RingAccess::singleWriter>(2000);
	RunModelAtCapacities<RingAccess::callerLocked>(3000);

	return VhalCheck::Finish("TestRingAccessModel");
}
