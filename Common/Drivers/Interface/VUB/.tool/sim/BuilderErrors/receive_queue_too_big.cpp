// expect: the receive queue holds at most 65535 packets
#include "SnippetSupport.h"


// A queue of 65536 slots has 16-bit indexes that wrap: the queue holds at most 65535 packets.
using Stack = VUB::Stack<+[] {
	return VUB::Builder()
		.Layer<TestLayer>()
		.Link<VUB::PacketEnd<64, 16>>()
		.MaxPayload(40)
		.ReceiveQueue(65536)
		.OnReceive<&SnippetReceive>()
		.Clock<&SnippetClock>();
}>;


static_assert(Stack::count == 2);
