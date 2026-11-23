// expect: a chain has more than one link
#include "SnippetSupport.h"


using Stack = Vub<+[] {
	return VubBuilder()
		.Layer<TestLayer>()
		.Link<VubPacketEnd<64, 16>>()
		.Link<VubPacketEnd<64, 16>>()
		.MaxPayload(40)
		.OnReceive<&SnippetReceive>()
		.Clock<&SnippetClock>();
}>;


static_assert(Stack::count == 3);
