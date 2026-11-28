// expect: PacketQueue::Clear is not offered in singleWriter mode
#include <Utilities/Buffer/PacketQueue.h>

// A singleWriter queue has no Clear: the consumer drops its packets with Drop.
void ClearSingleWriterQueue() {
	PacketQueue<8, 4, Memory::Inside, RingAccess::singleWriter> queue;
	queue.Clear();
}
