// expect: OnReceive<&Fn>() is missing
#include "SnippetSupport.h"


using Stack = Vub<+[] {
	return VubBuilder()
		.Layer<TestLayer>()
		.Link<VubPacketEnd<64, 16>>()
		.MaxPayload(40)
		.Clock<&SnippetClock>();
}>;


static_assert(Stack::count == 2);
