// Configuration (a): a packet link with MaxPayload(32) and the default queues. No layer, transform or framing.
#include "SizeWire.h"
#include <Drivers/Interface/VUB/Vub.h>


using SizeStack = Vub<+[] {
	return VubBuilder()
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
