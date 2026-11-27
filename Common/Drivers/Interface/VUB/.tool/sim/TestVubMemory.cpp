#include "SimCheck.h"
#include "ReliableChains.h"
#include <Drivers/Interface/VUB/TestKit/Loopback.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Memory/.test/SimCheck.h>
#include <Utilities/Memory/Heap.h>
#include <Utilities/Memory/Storage.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>


// The stacks of the memory policy: a stack whose buffers are in a heap, and one whose buffers are in the stack object. Both are
// Reliable chains over the same packet line. The heap of the first is large enough for every buffer; the heap of the second is too
// small for its control queue, so the stack is not ready.
alignas(8) std::uint8_t memoryTestArena[4096];
Memory::Heap<> memoryTestHeap(memoryTestArena, sizeof(memoryTestArena));

alignas(8) std::uint8_t memoryTinyArena[64];
Memory::Heap<> memoryTinyHeap(memoryTinyArena, sizeof(memoryTinyArena));


using HeapReliable = VUB::Stack<+[] {
	return VUB::Builder()
		.Memory<Memory::FromHeap<memoryTestHeap>>()
		.CountDrops()
		.SendQueue(4)
		.Layer<ReliableLayer>().Retries(3).TimeoutMs(200).Window(4).PayloadBytes(40).OnOutcome<&ReliableOutcome>().OnRestart<&ReliableRestart>()
		.Link<PacketEnd>()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.OnReceive<&ReliableReceive>()
		.Clock<&TestClock>();
}>;


using InsideReliable = VUB::Stack<+[] {
	return VUB::Builder()
		.CountDrops()
		.SendQueue(4)
		.Layer<ReliableLayer>().Retries(3).TimeoutMs(200).Window(4).PayloadBytes(40).OnOutcome<&ReliableOutcome>().OnRestart<&ReliableRestart>()
		.Link<PacketEnd>()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.OnReceive<&ReliableReceive>()
		.Clock<&TestClock>();
}>;


using TinyReliable = VUB::Stack<+[] {
	return VUB::Builder()
		.Memory<Memory::FromHeap<memoryTinyHeap>>()
		.CountDrops()
		.SendQueue(4)
		.Layer<ReliableLayer>().Retries(3).TimeoutMs(200).Window(4).PayloadBytes(40).OnOutcome<&ReliableOutcome>().OnRestart<&ReliableRestart>()
		.Link<PacketEnd>()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.OnReceive<&ReliableReceive>()
		.Clock<&TestClock>();
}>;


// One round of the line: the near stack processes, its packets reach the far stack, and its answers come back.
template<typename Line, typename NearStack, typename FarStack>
void MemoryRoundOnce(Line& line, NearStack& near, FarStack& far) {
	near.Process();
	line.DeliverToB(far);
	far.Process();
	line.DeliverToA(near);
	near.Process();
}


// A heap stack and an Inside stack on one line exchange packets both ways. Every packet is delivered once and acknowledged.
void TestHeapAndInsideExchangeBothWays() {
	ResetReliableState();
	VUB::PacketLine<64, 512> line(VUB::Faults{});
	HeapReliable near(line.a);
	GiveSession(near);
	InsideReliable far(line.b);
	GiveSession(far);
	SIM_EXPECT(near.Ready());
	SIM_EXPECT(far.Ready());

	// The first send of each stack is busy and starts its session: the data waits for the sync's acknowledgement.
	SIM_EXPECT(ReliableSend(near, 0xFFFF) == ResultStatus::busy);
	SIM_EXPECT(ReliableSend(far, 0xFFFF) == ResultStatus::busy);
	for (uint32 round = 0; round < 10; round++) {
		testNowUs = round * 1000;
		line.SetNow(testNowUs);
		MemoryRoundOnce(line, near, far);
	}

	SIM_EXPECT(ReliableSend(near, 1) == ResultStatus::ok);
	SIM_EXPECT(ReliableSend(far, 101) == ResultStatus::ok);
	for (uint32 round = 10; round < 60; round++) {
		testNowUs = round * 1000;
		line.SetNow(testNowUs);
		MemoryRoundOnce(line, near, far);
	}

	SIM_EXPECT(testDelivered[1] == 1);
	SIM_EXPECT(testDelivered[101] == 1);
	SIM_EXPECT(testStatus[1] == ResultStatus::ok);
	SIM_EXPECT(testStatus[101] == ResultStatus::ok);
	SIM_EXPECT(testRestarts == 0);
}


// A heap too small for the stack's buffers: the stack is not ready, Send refuses with notReady, and Process does nothing. The line
// carries nothing, and no handler runs.
void TestTooSmallHeapIsNotReady() {
	ResetReliableState();
	VUB::PacketLine<64, 512> line(VUB::Faults{});
	TinyReliable stack(line.a);
	GiveSession(stack);
	SIM_EXPECT(!stack.Ready());

	testNowUs = 0;
	line.SetNow(0);
	SIM_EXPECT(ReliableSend(stack, 5) == ResultStatus::notReady);
	uint32 pushedBefore = line.toB.Counts().pushed;
	for (uint32 round = 0; round < 20; round++) {
		testNowUs = round * 1000;
		line.SetNow(testNowUs);
		stack.Process();
	}

	SIM_EXPECT(line.toB.Counts().pushed == pushedBefore);
	SIM_EXPECT(testDelivered[5] == 0);
	SIM_EXPECT(testOutcomes[5] == 0);

	// A received packet is dropped by a stack that is not ready: the call returns with nothing taken, and the stack sends nothing.
	std::array<uint8, 4> packet = {1, 2, 3, 4};
	stack.FeedPacket(std::span<const uint8>(packet));
	stack.Process();
	SIM_EXPECT(line.toB.Counts().pushed == pushedBefore);
}


// A heap stack takes its buffers from the heap when it is built, and gives them back when it is destroyed.
void TestHeapStackReturnsItsBlocksOnDestruction() {
	ResetReliableState();
	VUB::PacketLine<64, 512> line(VUB::Faults{});
	std::size_t usedBefore = memoryTestHeap.UsedBytes();
	{
		HeapReliable stack(line.a);
		SIM_EXPECT(stack.Ready());
		SIM_EXPECT(memoryTestHeap.UsedBytes() > usedBefore);
	}

	SIM_EXPECT(memoryTestHeap.UsedBytes() == usedBefore);
}


int main() {
	TestHeapAndInsideExchangeBothWays();
	TestTooSmallHeapIsNotReady();
	TestHeapStackReturnsItsBlocksOnDestruction();
	return SimFinish("TestVubMemory");
}
