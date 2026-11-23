// expect: the receive ring holds at most 65535 bytes
#include "SnippetSupport.h"


// A ring of 65536 slots has 16-bit indexes that wrap: the ring holds at most 65535 bytes.
using Stack = Vub<+[] {
	return VubBuilder()
		.Layer<TestLayer>()
		.Transform<XorTransform>()
		.Framing<FlagFraming>()
		.Link<VubStreamEnd<64, 512>>()
		.MaxPayload(40)
		.ReceiveRing(65536)
		.OnReceive<&SnippetReceive>()
		.Clock<&SnippetClock>();
}>;


static_assert(Stack::count == 4);
