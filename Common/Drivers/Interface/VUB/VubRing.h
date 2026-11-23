#pragma once
#include <Utilities/DataTypes.h>
#include <Utilities/Result/ResultStatus.h>
#include <atomic>
#include <cstddef>
#include <span>


// The ring of received bytes and the queue of received packets. Both are plain inline functions on spans and indexes, not templates,
// so every stack in one image shares one copy of the code.
//
// Each index has one writer: the receive interrupt writes head, the main context writes tail. So no critical section is needed, on
// cores with or without atomic instructions. std::atomic_signal_fence keeps the compiler from moving a store of the data past the
// store of the index. A ring of n slots holds n - 1 bytes; a queue of n slots holds n - 1 packets.


// The two indexes of a ring or a queue.
struct VubIndexes {
	volatile uint16 head = 0;
	volatile uint16 tail = 0;
};


// Pushes one byte into the ring. Called from the receive interrupt. Returns false when the ring is full.
inline bool VubByteRingPush(VubIndexes& indexes, std::span<uint8> slots, uint8 value) {
	uint16 head = indexes.head;
	uint16 next = static_cast<uint16>((head + 1) % slots.size());
	if (next == indexes.tail) {
		return false;
	}

	slots[head] = value;
	std::atomic_signal_fence(std::memory_order_release);
	indexes.head = next;
	return true;
}


// Pops the oldest byte of the ring into value. Called from the main context. Returns false when the ring is empty.
inline bool VubByteRingPop(VubIndexes& indexes, std::span<const uint8> slots, uint8& value) {
	uint16 tail = indexes.tail;
	if (tail == indexes.head) {
		return false;
	}

	value = slots[tail];
	std::atomic_signal_fence(std::memory_order_acquire);
	indexes.tail = static_cast<uint16>((tail + 1) % slots.size());
	return true;
}


// The storage of a packet queue: slotBytes bytes per slot, and per slot the length and the flags of the packet. The lengths span has
// one entry per slot, so its size is the slot count.
struct VubQueueView {
	std::span<uint8> bytes;
	std::span<uint16> lengths;
	std::span<uint8> flags;
	std::size_t slotBytes;
};


// Whether the queue has no free slot.
inline bool VubQueueFull(const VubIndexes& indexes, std::size_t slotCount) {
	return static_cast<uint16>((indexes.head + 1) % slotCount) == indexes.tail;
}


// Whether the queue has no packet.
inline bool VubQueueEmpty(const VubIndexes& indexes) {
	return indexes.head == indexes.tail;
}


// Puts a packet at the back of the queue. messageTooLong when it is longer than a slot, busy when the queue is full. Called from the
// receive interrupt (for received packets) or the main context (for packets to send).
inline ResultStatus VubQueuePush(VubIndexes& indexes, const VubQueueView& view, std::span<const uint8> packet, uint8 packetFlags) {
	if (packet.size() > view.slotBytes) {
		return ResultStatus::messageTooLong;
	}

	uint16 head = indexes.head;
	if (VubQueueFull(indexes, view.lengths.size())) {
		return ResultStatus::busy;
	}

	for (std::size_t index = 0; index < packet.size(); index++) {
		view.bytes[head * view.slotBytes + index] = packet[index];
	}

	view.lengths[head] = static_cast<uint16>(packet.size());
	view.flags[head] = packetFlags;
	std::atomic_signal_fence(std::memory_order_release);
	indexes.head = static_cast<uint16>((head + 1) % view.lengths.size());
	return ResultStatus::ok;
}


// The oldest packet of the queue, without taking it out. Returns false when the queue is empty.
inline bool VubQueueFront(const VubIndexes& indexes, const VubQueueView& view, std::span<const uint8>& packet, uint8& packetFlags) {
	if (VubQueueEmpty(indexes)) {
		return false;
	}

	uint16 tail = indexes.tail;
	packet = std::span<const uint8>(view.bytes.data() + tail * view.slotBytes, view.lengths[tail]);
	packetFlags = view.flags[tail];
	return true;
}


// Drops the oldest packet. Call after the packet from VubQueueFront has been used.
inline void VubQueuePop(VubIndexes& indexes, std::size_t slotCount) {
	std::atomic_signal_fence(std::memory_order_acquire);
	indexes.tail = static_cast<uint16>((indexes.tail + 1) % slotCount);
}
