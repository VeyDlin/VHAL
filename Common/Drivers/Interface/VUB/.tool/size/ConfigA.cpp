// Configuration (a): a packet link with MaxPayload(32) and the default queues. No layer, transform or framing.
#include "SizeWire.h"
#include <Drivers/Interface/VUB/Stack.h>


using SizeStack = VUB::Stack<+[] {
	return VUB::Builder()
		.Link<SizePacketLink>()
		.MaxPayload(32)
		.OnReceive<&SizeReceive>()
		.Clock<&SizeClock>();
}>;


SizePacketLink sizeLink;
SizeStack sizeStack(sizeLink);


void SizeRun() {
	SizeRunPacket(sizeStack);
}
