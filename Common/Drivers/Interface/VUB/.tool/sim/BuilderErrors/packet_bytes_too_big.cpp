// expect: a packet of 65536 bytes or more does not fit the 16-bit lengths
#include "SnippetSupport.h"


// The packet is 65536 bytes (65533 payload and the 3-byte layer header): its length does not fit the 16-bit length fields.
using Stack = Vub<+[] {
	return VubBuilder()
		.Layer<TestLayer>()
		.Link<VubPacketEnd<65535, 16>>()
		.MaxPayload(65533)
		.OnReceive<&SnippetReceive>()
		.Clock<&SnippetClock>();
}>;


static_assert(Stack::count == 2);
