// Configuration (f): a byte-stream link with gap framing (default gap) and the CRC-16/MODBUS transform that uses the table engine.
#include "SizeWire.h"
#include <Drivers/Interface/VUB/Framing/VubGap.h>
#include <Drivers/Interface/VUB/Transform/VubCrc.h>
#include <Drivers/Interface/VUB/Vub.h>


using SizeStack = Vub<+[] {
	return VubBuilder()
		.Transform<VubCrc16Modbus>()
		.Framing<VubGap>()
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
