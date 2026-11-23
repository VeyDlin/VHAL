// expect: Retries(0) with the in-order option: a held packet is given up before its gap fills
#include "SnippetSupport.h"
#include <Drivers/Interface/VUB/Layer/VubReliable.h>


// With the in-order option a packet is acknowledged when it goes up. With no retry, a held packet is given up at its first timeout,
// before the gap in front of it fills, so the layer would stall.
using Stack = Vub<+[] {
	return VubBuilder()
		.Layer<VubReliableOf<40, 4, true>>().Retries(0)
		.Link<VubPacketEnd<64, 16>>()
		.OnReceive<&SnippetReceive>()
		.Clock<&SnippetClock>();
}>;


static_assert(Stack::count == 2);
