// expect: SendQueue() is zero
#include "SnippetSupport.h"


using Stack = Vub<+[] {
	return VubBuilder()
		.Layer<TestLayer>()
		.Link<VubPacketEnd<64, 16>>()
		.MaxPayload(40)
		.SendQueue(0)
		.OnReceive<&SnippetReceive>()
		.Clock<&SnippetClock>();
}>;


static_assert(Stack::count == 2);
