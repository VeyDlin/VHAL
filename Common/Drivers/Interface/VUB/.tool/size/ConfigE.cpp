// Configuration (e): a byte-stream link with a one-byte length prefix and the CRC-8 transform that uses the bitwise engine.
#include "SizeWire.h"
#include <Drivers/Interface/VUB/Engine/VubCrcBitwise.h>
#include <Drivers/Interface/VUB/Framing/VubLengthPrefix.h>
#include <Drivers/Interface/VUB/Transform/VubCrc.h>
#include <Drivers/Interface/VUB/Vub.h>


using SizeStack = Vub<+[] {
	return VubBuilder()
		.Transform<VubCrc<VubCrc8Smbus::parameters, VubCrcBitwise>>()
		.Framing<VubLengthPrefix<1>>()
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
