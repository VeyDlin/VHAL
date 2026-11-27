// expect: the receive ring holds at most 65535 bytes
#include "SnippetSupport.h"


// A ring of 65536 slots has 16-bit indexes that wrap: the ring holds at most 65535 bytes.
using Stack = VUB::Stack<+[] {
	return VUB::Builder()
		.Layer<TestLayer>()
		.Transform<XorTransform>()
		.Framing<FlagFraming>()
		.Link<VUB::StreamEnd<64, 512>>()
		.MaxPayload(40)
		.ReceiveRing(65536)
		.OnReceive<&SnippetReceive>()
		.Clock<&SnippetClock>();
}>;


static_assert(Stack::count == 4);
