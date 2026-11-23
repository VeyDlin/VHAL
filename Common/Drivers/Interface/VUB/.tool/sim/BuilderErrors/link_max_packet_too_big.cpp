// expect: the MaxPacket of the link is 65536 bytes or more
#include "SnippetSupport.h"


// The link's packets are at most 65535 bytes: the received lengths are 16-bit.
using Stack = VUB::Stack<+[] {
	return VUB::Builder()
		.Layer<TestLayer>()
		.Link<VUB::PacketEnd<65536, 16>>()
		.MaxPayload(40)
		.OnReceive<&SnippetReceive>()
		.Clock<&SnippetClock>();
}>;


static_assert(Stack::count == 2);
