// Configuration (e): a byte-stream link with a one-byte length prefix and the CRC-8 transform that uses the bitwise engine.
#include "SizeWire.h"
#include <Drivers/Interface/VUB/Engine/CrcBitwise.h>
#include <Drivers/Interface/VUB/Framing/LengthPrefix.h>
#include <Drivers/Interface/VUB/Transform/Crc.h>
#include <Drivers/Interface/VUB/Stack.h>


using SizeStack = VUB::Stack<+[] {
	return VUB::Builder()
		.Transform<VUB::Crc<VUB::Crc8Smbus::parameters, VUB::CrcBitwise>>()
		.Framing<VUB::LengthPrefix<1>>()
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
