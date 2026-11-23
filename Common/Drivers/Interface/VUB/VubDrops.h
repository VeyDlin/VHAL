#pragma once
#include <Utilities/DataTypes.h>


// The drop counters of a stack. Every packet or byte the stack discards is counted under one reason; nothing is dropped silently.
// The counters are 16-bit and stop at the maximum, to keep the RAM small.
struct VubDrops {
	uint16 ringFull = 0;        // bytes the receive interrupt could not store (stream links)
	uint16 queueFull = 0;       // packets the receive interrupt could not store (packet links)
	uint16 tooLong = 0;         // a frame or packet longer than the receive buffer
	uint16 framingError = 0;    // bytes that did not form a frame, or a frame whose checks failed
	uint16 transformError = 0;  // a transform refused a packet (a bad check, a bad length)
	uint16 layerError = 0;      // a layer refused a packet (a bad header, a bad length)
	uint16 sendError = 0;       // a packet the link or a framing refused for a reason other than busy
};


// Adds one to a counter, stopping at the maximum.
inline void VubCountDrop(uint16& counter) {
	if (counter < 65535) {
		counter++;
	}
}


// The same for a counter that the receive interrupt writes. The main context only reads it.
inline void VubCountIsr(volatile uint16& counter) {
	if (counter < 65535) {
		counter = static_cast<uint16>(counter + 1);
	}
}
