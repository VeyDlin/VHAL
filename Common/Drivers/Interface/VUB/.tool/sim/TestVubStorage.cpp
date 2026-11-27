#include <Utilities/Memory/.tool/sim/SimCheck.h>
#include <Drivers/Interface/VUB/Storage.h>
#include <Utilities/Memory/Heap.h>
#include <Utilities/Memory/Storage.h>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>


// The storages of the VUB layers: an enabled storage whose policy has no memory is not ready, and its spans are empty, so a caller
// that does not check Ready() gets no elements. The heap's host definitions come from the Memory simulation's check header.


#define SIM_EXPECT(condition) SIM_CHECK(condition)


alignas(8) std::uint8_t vubStorageTinyArena[64];
Memory::Heap<> vubStorageTinyHeap(vubStorageTinyArena, sizeof(vubStorageTinyArena));

alignas(8) std::uint8_t vubStorageArena[1024];
Memory::Heap<> vubStorageHeap(vubStorageArena, sizeof(vubStorageArena));


void TestBufferFromHeapTooSmall() {
	VUB::Buffer<true, 256, 1, Memory::FromHeap<vubStorageTinyHeap>> buffer;
	SIM_EXPECT(!buffer.Ready());
	SIM_EXPECT(buffer.Span().size() == 0);
	SIM_EXPECT(std::as_const(buffer).Span().size() == 0);
}


void TestBufferFromHeapReady() {
	VUB::Buffer<true, 32, 2, Memory::FromHeap<vubStorageHeap>> buffer;
	SIM_EXPECT(buffer.Ready());
	SIM_EXPECT(buffer.Span().size() == 32);
	buffer.Span()[31] = 9;
	SIM_EXPECT(std::as_const(buffer).Span()[31] == 9);
}


void TestRingStorageFromHeapTooSmall() {
	VUB::RingStorage<true, 512, 4, Memory::FromHeap<vubStorageTinyHeap>> ring;
	SIM_EXPECT(!ring.Ready());
	SIM_EXPECT(ring.Span().size() == 0);
}


void TestStampStorageFromHeapTooSmall() {
	VUB::StampStorage<true, 64, 5, Memory::FromHeap<vubStorageTinyHeap>> stamps;
	SIM_EXPECT(!stamps.Ready());
	SIM_EXPECT(stamps.Span().size() == 0);
	SIM_EXPECT(std::as_const(stamps).Span().size() == 0);
}


void TestQueueStorageFromHeapTooSmall() {
	VUB::QueueStorage<true, 8, 64, 6, Memory::FromHeap<vubStorageTinyHeap>> queue;
	SIM_EXPECT(!queue.Ready());
	VUB::QueueView view = queue.View();
	SIM_EXPECT(view.bytes.size() == 0);
	SIM_EXPECT(view.lengths.size() == 0);
	SIM_EXPECT(view.flags.size() == 0);
	SIM_EXPECT(view.slotBytes == 64);
}


void TestQueueStorageFromHeapReady() {
	VUB::QueueStorage<true, 4, 16, 7, Memory::FromHeap<vubStorageHeap>> queue;
	SIM_EXPECT(queue.Ready());
	VUB::QueueView view = queue.View();
	SIM_EXPECT(view.bytes.size() == 64);
	SIM_EXPECT(view.lengths.size() == 4);
	SIM_EXPECT(view.flags.size() == 4);
}


void TestDisabledStoragesAreReady() {
	VUB::Buffer<false, 256, 8, Memory::FromHeap<vubStorageTinyHeap>> buffer;
	SIM_EXPECT(buffer.Ready());
	SIM_EXPECT(buffer.Span().size() == 0);

	VUB::QueueStorage<false, 8, 64, 9, Memory::FromHeap<vubStorageTinyHeap>> queue;
	SIM_EXPECT(queue.Ready());
}


int main() {
	TestBufferFromHeapTooSmall();
	TestBufferFromHeapReady();
	TestRingStorageFromHeapTooSmall();
	TestStampStorageFromHeapTooSmall();
	TestQueueStorageFromHeapTooSmall();
	TestQueueStorageFromHeapReady();
	TestDisabledStoragesAreReady();
	return VstdSim::Finish("TestVubStorage");
}
