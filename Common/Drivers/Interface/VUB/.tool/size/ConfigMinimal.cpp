// Configuration of (b) with the CRC-16/MODBUS transform on the default engine, and nothing else: no send queue and no drop counters. The
// flag SIZE_OPTIONS adds builder calls to the chain (for example -DSIZE_OPTIONS=.SendQueue(4).CountDrops()); it is empty by default.
#ifndef SIZE_OPTIONS
#define SIZE_OPTIONS
#endif

#include "SizeWire.h"
#include <Drivers/Interface/VUB/Framing/Cobs.h>
#include <Drivers/Interface/VUB/Transform/Crc.h>
#include <Drivers/Interface/VUB/Stack.h>
#include <Utilities/Checksum/CRC/Preset/Crc16Modbus.h>


using SizeStack = VUB::Stack<+[] {
	return VUB::Builder()
		SIZE_OPTIONS
		.Transform<VUB::Crc<Checksum::Crc16Modbus>>()
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
