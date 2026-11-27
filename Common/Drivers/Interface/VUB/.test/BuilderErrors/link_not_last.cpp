// expect: the link must be the last block of the chain
#include "SnippetSupport.h"


using Stack = VUB::Stack<+[] {
	return VUB::Builder()
		.Layer<TestLayer>()
		.Link<VUB::PacketEnd<64, 16>>()
		.Transform<XorTransform>()
		.MaxPayload(40)
		.OnReceive<&SnippetReceive>()
		.Clock<&SnippetClock>();
}>;


static_assert(Stack::count == 3);
