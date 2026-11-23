// expect: MaxPayload() is not given
#include "SnippetSupport.h"


using Stack = Vub<+[] {
	return VubBuilder()
		.Layer<TestLayer>()
		.Link<VubPacketEnd<64, 16>>()
		.OnReceive<&SnippetReceive>()
		.Clock<&SnippetClock>();
}>;


static_assert(Stack::count == 2);
