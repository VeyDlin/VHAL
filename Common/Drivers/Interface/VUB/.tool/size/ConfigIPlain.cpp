// Breakdown of configuration (i): the same COBS framing and CRC-8 with no VUB::ReliableOf layer. The difference from (i) is the layer's cost.
#include "SizeWire.h"
#include <Drivers/Interface/VUB/Framing/Cobs.h>
#include <Drivers/Interface/VUB/Transform/Crc.h>
#include <Drivers/Interface/VUB/Stack.h>


using SizeStack = VUB::Stack<+[] {
	return VUB::Builder()
		.Transform<VUB::Crc<Checksum::Crc<Checksum::CrcParameters().Width(8).Polynomial(0x07).Initial(0xFF).FinalXor(0x00).Check(0xFB)>>>()
		.Framing<VUB::Cobs>()
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
