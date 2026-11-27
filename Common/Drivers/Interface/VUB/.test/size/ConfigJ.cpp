// Configuration (j): (c) with VUB::KeepAlive above the transform, with its default times (quiet 1 s, down 3 s).
#include "SizeWire.h"
#include <Drivers/Interface/VUB/Framing/Cobs.h>
#include <Drivers/Interface/VUB/Layer/KeepAlive.h>
#include <Drivers/Interface/VUB/Transform/Crc.h>
#include <Drivers/Interface/VUB/Stack.h>
#include <Utilities/Checksum/CRC/Preset/Crc16Modbus.h>


using SizeStack = VUB::Stack<+[] {
	return VUB::Builder()
		.Layer<VUB::KeepAlive>()
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
