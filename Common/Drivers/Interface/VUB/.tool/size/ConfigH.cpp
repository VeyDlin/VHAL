// Configuration (h): (g) with the in-order option of VUB::Reliable: Retries 3, Window 4, the default timeout.
#include "SizeWire.h"
#include <Drivers/Interface/VUB/Framing/Cobs.h>
#include <Drivers/Interface/VUB/Layer/Reliable.h>
#include <Drivers/Interface/VUB/Transform/Crc.h>
#include <Drivers/Interface/VUB/Stack.h>


using SizeStack = VUB::Stack<+[] {
	return VUB::Builder()
		.Layer<VUB::ReliableOf<32, 4, true>>().Retries(3).Window(4)
		.Transform<VUB::Crc16Modbus>()
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
	sizeStack.Find<VUB::ReliableOf<32, 4, true>>().SetSession(0x1234);
	SizeRunReliable(sizeStack);
}
