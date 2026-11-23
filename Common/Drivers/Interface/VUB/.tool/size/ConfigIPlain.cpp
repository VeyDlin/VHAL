// Breakdown of configuration (i): the same COBS framing and CRC-8 with no VubReliableOf layer. The difference from (i) is the layer's cost.
#include "SizeWire.h"
#include <Drivers/Interface/VUB/Engine/VubCrcBitwise.h>
#include <Drivers/Interface/VUB/Framing/VubCobs.h>
#include <Drivers/Interface/VUB/Transform/VubCrc.h>
#include <Drivers/Interface/VUB/Vub.h>


using SizeStack = Vub<+[] {
	return VubBuilder()
		.Transform<VubCrc<VubCrcParameters{8, 0x07, 0xFF, 0x00, false, false, 0xFB}, VubCrcBitwise>>()
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
