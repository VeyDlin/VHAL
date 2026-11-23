// expect: the framing must be right above the link
#include "SnippetSupport.h"


using Stack = Vub<+[] {
	return VubBuilder()
		.Framing<FlagFraming>()
		.Layer<TestLayer>()
		.Link<VubStreamEnd<64, 16>>()
		.MaxPayload(40)
		.OnReceive<&SnippetReceive>()
		.Clock<&SnippetClock>();
}>;


static_assert(Stack::count == 3);
