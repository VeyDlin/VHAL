// expect: PacketQueue::DropFlagged writes the head
#include <Utilities/Buffer/PacketQueue.h>

// DropFlagged writes the head, which the producer owns in singleWriter mode.
void DropFlaggedSingleWriterQueue() {
	PacketQueue<8, 4, Memory::Inside, RingAccess::singleWriter> queue;
	queue.DropFlagged(0x01);
}
