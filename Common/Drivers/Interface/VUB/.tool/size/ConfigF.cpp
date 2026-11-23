// Configuration (f): a byte-stream link with gap framing (default gap) and the CRC-16/MODBUS transform that uses the table engine.
#include "SizeWire.h"
#include <Drivers/Interface/VUB/Framing/Gap.h>
#include <Drivers/Interface/VUB/Transform/Crc.h>
#include <Drivers/Interface/VUB/Stack.h>


using SizeStack = VUB::Stack<+[] {
	return VUB::Builder()
		.Transform<VUB::Crc16Modbus>()
		.Framing<VUB::Gap>()
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
