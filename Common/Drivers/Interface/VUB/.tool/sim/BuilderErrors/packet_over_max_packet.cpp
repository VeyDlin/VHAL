// expect: a packet is larger than the MaxPacket of the link
#include "SnippetSupport.h"


// 40 payload + 3 layer header + 2 XOR growth = 45 bytes, but the link carries 44.
using Stack = Vub<+[] {
	return VubBuilder()
		.Layer<TestLayer>()
		.Transform<XorTransform>()
		.Link<VubPacketEnd<44, 16>>()
		.MaxPayload(40)
		.OnReceive<&SnippetReceive>()
		.Clock<&SnippetClock>();
}>;


static_assert(Stack::count == 3);
