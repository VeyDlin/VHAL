// expect: Retries(0) with the in-order option: a held packet is given up before its gap fills
#include "SnippetSupport.h"
#include <Drivers/Interface/VUB/Layer/Reliable.h>


// With the in-order option a packet is acknowledged when it goes up. With no retry, a held packet is given up at its first timeout,
// before the gap in front of it fills, so the layer would stall.
using Stack = VUB::Stack<+[] {
	return VUB::Builder()
		.Layer<VUB::ReliableOf<40, 4, true>>().Retries(0)
		.Link<VUB::PacketEnd<64, 16>>()
		.OnReceive<&SnippetReceive>()
		.Clock<&SnippetClock>();
}>;


static_assert(Stack::count == 2);
