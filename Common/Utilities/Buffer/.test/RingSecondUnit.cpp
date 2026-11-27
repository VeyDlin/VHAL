#include <VHAL.h>
#include <Utilities/Buffer/RingBuffer.h>
#include <Utilities/Memory/Heap.h>
#include <Utilities/Memory/Storage.h>

// A second translation unit for TestRingBufferStorage. Its ring is built from the heap of the test unit. The ring is a
// function-local static, so it is constructed on the first call, after main has built the heap.


extern Memory::Heap<> ringTestHeap;


int RingSecondUnitPushes() {
	static RingBuffer<uint8, 16, Memory::FromHeap<ringTestHeap>> ring;
	ring.Push(static_cast<uint8>(1));
	ring.Push(static_cast<uint8>(2));
	ring.Push(static_cast<uint8>(3));
	return ring.Size();
}
