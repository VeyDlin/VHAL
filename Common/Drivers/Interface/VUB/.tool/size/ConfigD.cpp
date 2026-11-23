// Configuration (d): (b) with the CRC-16/MODBUS transform that uses the bitwise engine.
#include "SizeWire.h"
#include <Drivers/Interface/VUB/Engine/VubCrcBitwise.h>
#include <Drivers/Interface/VUB/Framing/VubCobs.h>
#include <Drivers/Interface/VUB/Transform/VubCrc.h>
#include <Drivers/Interface/VUB/Vub.h>


using SizeStack = Vub<+[] {
	return VubBuilder()
		.Transform<VubCrc<VubCrc16Modbus::parameters, VubCrcBitwise>>()
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
