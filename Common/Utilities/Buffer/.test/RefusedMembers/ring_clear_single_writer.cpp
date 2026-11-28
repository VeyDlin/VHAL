// expect: RingBuffer::Clear is not offered in singleWriter mode
#include <Utilities/Buffer/RingBuffer.h>

// A singleWriter ring has no Clear: the consumer empties it with Drop.
void ClearSingleWriterRing() {
	RingBuffer<uint8, 4, Memory::Inside, RingAccess::singleWriter> ring;
	ring.Clear();
}
