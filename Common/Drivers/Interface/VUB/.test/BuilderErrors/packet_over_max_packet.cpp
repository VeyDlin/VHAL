// expect: a packet is larger than the MaxPacket of the link
#include "SnippetSupport.h"


// 40 payload + 3 layer header + 2 XOR growth = 45 bytes, but the link carries 44.
using Stack = VUB::Stack<+[] {
	return VUB::Builder()
		.Layer<TestLayer>()
		.Transform<XorTransform>()
		.Link<VUB::PacketEnd<44, 16>>()
		.MaxPayload(40)
		.OnReceive<&SnippetReceive>()
		.Clock<&SnippetClock>();
}>;


static_assert(Stack::count == 3);
