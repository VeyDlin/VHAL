// expect: a packet link takes no framing
#include "SnippetSupport.h"


using Stack = Vub<+[] {
	return VubBuilder()
		.Layer<TestLayer>()
		.Framing<FlagFraming>()
		.Link<VubPacketEnd<64, 16>>()
		.MaxPayload(40)
		.OnReceive<&SnippetReceive>()
		.Clock<&SnippetClock>();
}>;


static_assert(Stack::count == 3);
