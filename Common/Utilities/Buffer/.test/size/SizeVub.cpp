#include <Drivers/Interface/VUB/Ring.h>
#include <Utilities/DataTypes.h>
#include <cstddef>
#include <span>

// The ring form of VUB's Ring.h, for the record: volatile indexes and std::atomic_signal_fence, a ring of 64 slots. It holds 63 bytes.
// The buffer of 32 bytes is pushed and popped, as in the other size programs.


static VUB::Indexes indexes;


static uint8 slots[64];


uint64 SizeRun(const uint8* data, std::size_t length) {
	for (std::size_t index = 0; index < length; index++) {
		VUB::ByteRingPush(indexes, std::span<uint8>(slots), data[index]);
	}

	uint64 sum = 0;
	uint8 value = 0;
	for (std::size_t index = 0; index < length; index++) {
		if (VUB::ByteRingPop(indexes, std::span<const uint8>(slots), value)) {
			sum += value;
		}
	}

	return sum;
}
