#include <VHAL.h>
#include <VhalCheck.h>
#include <Utilities/Buffer/PacketQueue.h>
#include <Utilities/Buffer/RingBuffer.h>
#include <Utilities/Memory/Storage.h>
#include <span>

// The examples of the access modes, of the indexed members and of PacketQueue on the Buffer wiki page, run as the page writes them.


void ReceiveInterruptAndMainLoop() {
	RingBuffer<uint8, 256, Memory::Inside, RingAccess::singleWriter> rxBytes;

	// The receive interrupt pushes the byte it received.
	rxBytes.Push(static_cast<uint8>(0x5A));

	// The main loop pops it.
	Result<uint8> byte = rxBytes.Pop();
	VHAL_CHECK(byte.IsOk());
	VHAL_CHECK_EQ(byte.Value(), static_cast<uint8>(0x5A));
}


void ConsumerDropsInSingleWriter() {
	RingBuffer<uint8, 8, Memory::Inside, RingAccess::singleWriter> ring;
	ring.Push(static_cast<uint8>(1));
	ring.Push(static_cast<uint8>(2));

	// The consumer empties the ring with Drop, since Clear is not offered in this mode.
	VHAL_CHECK_EQ(ring.Drop(ring.Size()), static_cast<uint16>(2));
	VHAL_CHECK(ring.IsEmpty());
}


void LookAndDrop() {
	RingBuffer<uint8, 8, Memory::Inside, RingAccess::callerLocked> ring;
	ring.Push(static_cast<uint8>(1));
	ring.Push(static_cast<uint8>(2));
	ring.Push(static_cast<uint8>(3));

	const uint8* oldest = ring.Front(); // *oldest == 1
	const uint8* second = ring.Peek(1); // *second == 2
	const uint8* none = ring.Peek(3);   // nullptr, only three elements are held
	VHAL_CHECK_EQ(*oldest, static_cast<uint8>(1));
	VHAL_CHECK_EQ(*second, static_cast<uint8>(2));
	VHAL_CHECK(none == nullptr);

	ring.Drop(2); // removes 1 and 2; the front is 3
	VHAL_CHECK_EQ(*ring.Front(), static_cast<uint8>(3));
}


void ExternalNeedsOneSlotMore() {
	// A singleWriter ring of 256 elements over memory of 257 bytes.
	uint8 memory[257] = {};
	RingBuffer<uint8, 256, Memory::External, RingAccess::singleWriter> rx{std::span<uint8>(memory)};
	VHAL_CHECK_EQ(rx.Push(static_cast<uint8>(7)), ResultStatus::ok);
	VHAL_CHECK_EQ(memory[0], static_cast<uint8>(7));
}


void PacketQueueBasics() {
	PacketQueue<64, 8> packets; // up to 8 packets of 64 bytes

	const uint8 command[] = {0x01, 0x02, 0x03};
	packets.Push(std::span<const uint8>(command), 0x00); // the flags byte of the packet

	std::span<const uint8> packet;
	uint8 flags = 0;
	if (packets.Front(packet, flags) == ResultStatus::ok) {
		VHAL_CHECK_EQ(packet.size(), static_cast<std::size_t>(3));
		VHAL_CHECK_EQ(packet[2], static_cast<uint8>(0x03));
	}

	packets.Pop();
	VHAL_CHECK(packets.Empty());
}


void PacketQueueDropFlagged() {
	PacketQueue<64, 8, Memory::Inside, RingAccess::locked> queue;

	const uint8 urgent[] = {0xEE};
	const uint8 normal[] = {0x11};
	queue.Push(std::span<const uint8>(urgent), 0x01);
	queue.Push(std::span<const uint8>(normal), 0x00);

	uint16 dropped = queue.DropFlagged(0x01); // the urgent packet is dropped, the normal one stays
	VHAL_CHECK_EQ(dropped, static_cast<uint16>(1));
	VHAL_CHECK_EQ(queue.Count(), static_cast<uint16>(1));
}


void PacketQueueExternal() {
	// Eight slots of 64 bytes are held, so the memory needs eight slots: 8 * 64 bytes, and eight entries of lengths and flags.
	uint8 bytes[8 * 64] = {};
	uint8 lengths[8] = {};
	uint8 flags[8] = {};

	PacketQueue<64, 8, Memory::External, RingAccess::callerLocked> queue{
		std::span<uint8>(bytes),
		std::span<uint8>(lengths),
		std::span<uint8>(flags)};
	const uint8 packet[] = {0x42};
	VHAL_CHECK_EQ(queue.Push(std::span<const uint8>(packet), 0x00), ResultStatus::ok);
	VHAL_CHECK_EQ(lengths[0], static_cast<uint8>(1));
}


int main() {
	ReceiveInterruptAndMainLoop();
	ConsumerDropsInSingleWriter();
	LookAndDrop();
	ExternalNeedsOneSlotMore();
	PacketQueueBasics();
	PacketQueueDropFlagged();
	PacketQueueExternal();

	return VhalCheck::Finish("TestRingAccessReadme");
}
