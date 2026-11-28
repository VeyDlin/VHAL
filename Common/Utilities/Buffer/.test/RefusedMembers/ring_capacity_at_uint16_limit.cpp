// expect: keeps N + 1 indexes: N must be below 65535
#include <Utilities/Buffer/RingBuffer.h>

// A singleWriter ring of 65535 elements needs 65536 slots for its indexes, which uint16 cannot count.
RingBuffer<uint8, 65535, Memory::Inside, RingAccess::singleWriter> tooLargeRing;
