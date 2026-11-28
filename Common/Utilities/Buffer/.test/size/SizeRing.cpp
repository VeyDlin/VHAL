#include <Utilities/Buffer/RingBuffer.h>
#include <Utilities/DataTypes.h>
#include <cstddef>

// One size program of a ring of 64 bytes, in the access mode named by SIZE_ACCESS. The program pushes the buffer into the ring, then reads
// each element in place with Front() and removes it with Drop(1). The image holds the code of Push, Front and Drop and the storage of
// the ring. The critical section of a port is stubbed here: the stub is empty, so the locked row shows the ring's own code and the
// calls to the section, and not the port's section.


void System::CriticalSection(bool) {
}


static RingBuffer<uint8, 64, Memory::Inside, RingAccess::SIZE_ACCESS> ring;


uint64 SizeRun(const uint8* data, std::size_t length) {
	uint64 sum = 0;
	for (std::size_t index = 0; index < length; index++) {
		ring.Push(data[index]);
	}

	for (std::size_t index = 0; index < length; index++) {
		const uint8* front = ring.Front();
		if (front != nullptr) {
			sum += *front;
			ring.Drop(1);
		}
	}

	return sum;
}
