// expect: ReceiveRing() is zero
#include "SnippetSupport.h"


using Stack = Vub<+[] {
	return VubBuilder()
		.Layer<TestLayer>()
		.Framing<FlagFraming>()
		.Link<VubStreamEnd<64, 16>>()
		.MaxPayload(40)
		.ReceiveRing(0)
		.OnReceive<&SnippetReceive>()
		.Clock<&SnippetClock>();
}>;


static_assert(Stack::count == 3);
