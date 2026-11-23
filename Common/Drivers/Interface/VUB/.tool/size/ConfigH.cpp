// Configuration (h): (g) with the in-order option of VubReliable: Retries 3, Window 4, the default timeout.
#include "SizeWire.h"
#include <Drivers/Interface/VUB/Framing/VubCobs.h>
#include <Drivers/Interface/VUB/Layer/VubReliable.h>
#include <Drivers/Interface/VUB/Transform/VubCrc.h>
#include <Drivers/Interface/VUB/Vub.h>


using SizeStack = Vub<+[] {
	return VubBuilder()
		.Layer<VubReliableOf<32, 4, true>>().Retries(3).Window(4)
		.Transform<VubCrc16Modbus>()
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
