// Configuration (b): a byte-stream link with COBS framing, MaxPayload(32) and the default ring.
#include "SizeWire.h"
#include <Drivers/Interface/VUB/Framing/VubCobs.h>
#include <Drivers/Interface/VUB/Vub.h>


using SizeStack = Vub<+[] {
	return VubBuilder()
		.Framing<VubCobs>()
		.Link<SizeStreamLink>()
		.MaxPayload(32)
		.OnReceive<&SizeReceive>()
		.Clock<&SizeClock>();
}>;


SizeStreamLink sizeLink;
SizeStack sizeStack(sizeLink);


void SizeRun() {
	SizeRunStream(sizeStack);
}
