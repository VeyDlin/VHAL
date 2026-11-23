// expect: ReceiveRing() is zero
#include "SnippetSupport.h"


using Stack = VUB::Stack<+[] {
	return VUB::Builder()
		.Layer<TestLayer>()
		.Framing<FlagFraming>()
		.Link<VUB::StreamEnd<64, 16>>()
		.MaxPayload(40)
		.ReceiveRing(0)
		.OnReceive<&SnippetReceive>()
		.Clock<&SnippetClock>();
}>;


static_assert(Stack::count == 3);
