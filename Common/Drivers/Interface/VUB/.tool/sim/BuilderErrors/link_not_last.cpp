// expect: the link must be the last block of the chain
#include "SnippetSupport.h"


using Stack = Vub<+[] {
	return VubBuilder()
		.Layer<TestLayer>()
		.Link<VubPacketEnd<64, 16>>()
		.Transform<XorTransform>()
		.MaxPayload(40)
		.OnReceive<&SnippetReceive>()
		.Clock<&SnippetClock>();
}>;


static_assert(Stack::count == 3);
