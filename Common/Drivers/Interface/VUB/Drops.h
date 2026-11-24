#pragma once
#include <Utilities/DataTypes.h>
#include <cstddef>

namespace VUB {


// The drop counters the stack keeps. Every packet or byte the stack discards is counted under one reason; nothing is dropped silently.
// The counters are 16-bit and stop at the maximum, to keep the RAM small.
struct DropCounts {
	uint16 ringFull = 0;        // bytes the receive interrupt could not store (stream links)
	uint16 queueFull = 0;       // packets the receive interrupt could not store (packet links)
	uint16 tooLong = 0;         // a frame or packet longer than the receive buffer
	uint16 framingError = 0;    // bytes that did not form a frame, or a frame whose checks failed
	uint16 transformError = 0;  // a transform refused a packet (a bad check, a bad length)
	uint16 layerError = 0;      // a layer refused a packet (a bad header, a bad length)
	uint16 sendError = 0;       // a packet the link or a framing refused for a reason other than busy
};


// The drop counters as Drops() reports them. linkDown counts the packets that were queued when the connection went down under the Fail
// option, and the bytes of the receive ring that a down discarded. It is zero on a stack that cannot see the connection go down.
struct DropCounters : DropCounts {
	uint16 linkDown = 0;
};


// The counters a layer gets from Outbox::Counters() when it declares usesCounters = true: the drop counters, the resends and peer restarts
// of Reliable (zero unless Reliable is Counted()), and the highest fill of the receive ring and of the control queue, saturating at 255
// (zero unless the stack is HighWater()).
struct StackCounters {
	DropCounters drops;
	uint16 resends = 0;
	uint16 peerRestarts = 0;
	uint8 highWaterRing = 0;
	uint8 highWaterControl = 0;
};


// Adds one to a counter, stopping at the maximum.
inline void CountDrop(uint16& counter) {
	if (counter < 65535) {
		counter++;
	}
}


// Adds amount to a counter, stopping at the maximum.
inline void AddDrops(uint16& counter, std::size_t amount) {
	std::size_t total = static_cast<std::size_t>(counter) + amount;
	counter = total > 65535 ? static_cast<uint16>(65535) : static_cast<uint16>(total);
}


// The same for a counter that the receive interrupt writes. The main context only reads it.
inline void CountIsr(volatile uint16& counter) {
	if (counter < 65535) {
		counter = static_cast<uint16>(counter + 1);
	}
}

}
