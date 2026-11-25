// Configuration (d): (b) with the CRC-16/MODBUS transform that names the table engine.
#include "SizeWire.h"
#include <Drivers/Interface/VUB/Framing/Cobs.h>
#include <Drivers/Interface/VUB/Transform/Crc.h>
#include <Drivers/Interface/VUB/Stack.h>
#include <Utilities/Checksum/CRC/Engine/CrcTable.h>
#include <Utilities/Checksum/CRC/Preset/Crc16Modbus.h>


using SizeStack = VUB::Stack<+[] {
	return VUB::Builder()
		.Transform<VUB::Crc<Checksum::Crc16Modbus::With<Checksum::CrcTable>>>()
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
