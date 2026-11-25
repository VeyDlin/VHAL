// Configuration (i): the properties CompactStreamingProtocol gives the bootloader, as a VUB stack. COBS framing, the CRC-8 with
// polynomial 0x07 and initial value 0xFF (the bootloader's CRC) on the bitwise engine, VUB::ReliableOf with a window of 1 and 3 retries.
#include "SizeWire.h"
#include <Drivers/Interface/VUB/Framing/Cobs.h>
#include <Drivers/Interface/VUB/Layer/Reliable.h>
#include <Drivers/Interface/VUB/Transform/Crc.h>
#include <Drivers/Interface/VUB/Stack.h>


using SizeStack = VUB::Stack<+[] {
	return VUB::Builder()
		.Layer<VUB::ReliableOf<32, 1>>().Retries(3).Window(1)
		.Transform<VUB::Crc<Checksum::Crc<Checksum::CrcParameters().Width(8).Polynomial(0x07).Initial(0xFF).FinalXor(0x00).Check(0xFB)>>>()
		.Framing<VUB::Cobs>()
		.Link<SizeStreamLink>()
		.MaxPayload(32)
		.OnReceive<&SizeReceive>()
		.Clock<&SizeClock>();
}>;


SizeStreamLink sizeLink;
SizeStack sizeStack(sizeLink);


// The application gives the reliable layer its boot value once, before the first send: until then Send returns noInit and nothing is sent.
void SizeRun() {
	sizeStack.Find<VUB::ReliableOf<32, 1>>().SetSession(0x1234);
	SizeRunReliable(sizeStack);
}
