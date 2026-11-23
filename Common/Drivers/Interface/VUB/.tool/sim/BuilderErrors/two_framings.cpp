// expect: a chain has two framings
#include "SnippetSupport.h"


using Stack = VUB::Stack<+[] {
	return VUB::Builder()
		.Layer<TestLayer>()
		.Framing<FlagFraming>()
		.Framing<FlagFraming>()
		.Link<VUB::StreamEnd<64, 16>>()
		.MaxPayload(40)
		.OnReceive<&SnippetReceive>()
		.Clock<&SnippetClock>();
}>;


static_assert(Stack::count == 4);
