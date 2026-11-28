#include <Utilities/Buffer/PacketQueue.h>
#include <Utilities/DataTypes.h>
#include <cstddef>
#include <span>

// One size program of a packet queue of 8 slots of 64 bytes, in the access mode named by SIZE_ACCESS. The program pushes the buffer as
// packets of 4 bytes and pops them, so the image holds the code of Push, Front and Pop and the storage. The critical section of a port
// is stubbed, as in SizeRing.cpp.


void System::CriticalSection(bool) {
}


static PacketQueue<64, 8, Memory::Inside, RingAccess::SIZE_ACCESS> queue;


uint64 SizeRun(const uint8* data, std::size_t length) {
	uint64 sum = 0;
	for (std::size_t index = 0; index + 4 <= length; index += 4) {
		queue.Push(std::span<const uint8>(data + index, 4), static_cast<uint8>(index));
	}

	std::span<const uint8> packet;
	uint8 flags = 0;
	while (queue.Front(packet, flags) == ResultStatus::ok) {
		sum += packet[0] + flags;
		queue.Pop();
	}

	return sum;
}
