#pragma once
#include <Utilities/DataTypes.h>
#include <Utilities/Result/ResultStatus.h>
#include <atomic>
#include <cstddef>
#include <span>

namespace VUB {


// The ring of received bytes and the queue of received packets. Both are plain inline functions on spans and indexes, not templates,
// so every stack in one image shares one copy of the code.
//
// Each index has one writer: the receive interrupt writes head, the main context writes tail. So no critical section is needed, on
// cores with or without atomic instructions. std::atomic_signal_fence keeps the compiler from moving a store of the data past the
// store of the index. A ring of n slots holds n - 1 bytes; a queue of n slots holds n - 1 packets.


// The two indexes of a ring or a queue.
struct Indexes {
	volatile uint16 head = 0;
	volatile uint16 tail = 0;
};


// Pushes one byte into the ring. Called from the receive interrupt. Returns false when the ring is full.
inline bool ByteRingPush(Indexes& indexes, std::span<uint8> slots, uint8 value) {
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


// Pushes one byte and the time it was received, for a chain whose framing uses time. Called from the receive interrupt. Returns false
// when the ring is full.
inline bool TimedRingPush(Indexes& indexes, std::span<uint8> slots, std::span<uint32> stamps, uint8 value, uint32 stamp) {
	uint16 head = indexes.head;
	uint16 next = static_cast<uint16>((head + 1) % slots.size());
	if (next == indexes.tail) {
		return false;
	}

	slots[head] = value;
	stamps[head] = stamp;
	std::atomic_signal_fence(std::memory_order_release);
	indexes.head = next;
	return true;
}


// Pops the oldest byte of a timed ring into value, with its time. Called from the main context. Returns false when the ring is empty.
inline bool TimedRingPop(Indexes& indexes, std::span<const uint8> slots, std::span<const uint32> stamps, uint8& value,
	uint32& stamp) {
	uint16 tail = indexes.tail;
	if (tail == indexes.head) {
		return false;
	}

	value = slots[tail];
	stamp = stamps[tail];
	std::atomic_signal_fence(std::memory_order_acquire);
	indexes.tail = static_cast<uint16>((tail + 1) % slots.size());
	return true;
}


// Drops the bytes the ring holds, and returns how many there were. Called from the main context when the link goes down: the receive
// interrupt keeps writing the head, the main context moves the tail up to it.
inline std::size_t ByteRingClear(Indexes& indexes, std::size_t slotCount) {
	uint16 head = indexes.head;
	uint16 tail = indexes.tail;
	std::size_t held = (static_cast<std::size_t>(head) + slotCount - tail) % slotCount;
	indexes.tail = head;
	return held;
}


// Pops the oldest byte of the ring into value. Called from the main context. Returns false when the ring is empty.
inline bool ByteRingPop(Indexes& indexes, std::span<const uint8> slots, uint8& value) {
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
struct QueueView {
	std::span<uint8> bytes;
	std::span<uint16> lengths;
	std::span<uint8> flags;
	std::size_t slotBytes;
};


// Whether the queue has no free slot.
inline bool QueueFull(const Indexes& indexes, std::size_t slotCount) {
	return static_cast<uint16>((indexes.head + 1) % slotCount) == indexes.tail;
}


// Whether the queue has no packet.
inline bool QueueEmpty(const Indexes& indexes) {
	return indexes.head == indexes.tail;
}


// Puts a packet at the back of the queue. messageTooLong when it is longer than a slot, busy when the queue is full. Called from the
// receive interrupt (for received packets) or the main context (for packets to send).
inline ResultStatus QueuePush(Indexes& indexes, const QueueView& view, std::span<const uint8> packet, uint8 packetFlags) {
	if (packet.size() > view.slotBytes) {
		return ResultStatus::messageTooLong;
	}

	uint16 head = indexes.head;
	if (QueueFull(indexes, view.lengths.size())) {
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
inline bool QueueFront(const Indexes& indexes, const QueueView& view, std::span<const uint8>& packet, uint8& packetFlags) {
	if (QueueEmpty(indexes)) {
		return false;
	}

	uint16 tail = indexes.tail;
	packet = std::span<const uint8>(view.bytes.data() + tail * view.slotBytes, view.lengths[tail]);
	packetFlags = view.flags[tail];
	return true;
}


// Drops the oldest packet. Call after the packet from QueueFront has been used.
inline void QueuePop(Indexes& indexes, std::size_t slotCount) {
	std::atomic_signal_fence(std::memory_order_acquire);
	indexes.tail = static_cast<uint16>((indexes.tail + 1) % slotCount);
}

}
