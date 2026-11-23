// Configuration (k): (g) with VUB::KeepAlive below VUB::Reliable and above the transform, with its default times (quiet 1 s, down 3 s).
#include "SizeWire.h"
#include <Drivers/Interface/VUB/Framing/Cobs.h>
#include <Drivers/Interface/VUB/Layer/KeepAlive.h>
#include <Drivers/Interface/VUB/Layer/Reliable.h>
#include <Drivers/Interface/VUB/Transform/Crc.h>
#include <Drivers/Interface/VUB/Stack.h>


using SizeStack = VUB::Stack<+[] {
	return VUB::Builder()
		.Layer<VUB::Reliable>().Retries(3).Window(4)
		.Layer<VUB::KeepAlive>()
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
	sizeStack.Find<VUB::Reliable>().SetSession(0x1234);
	SizeRunReliable(sizeStack);
}
