// expect: the framing cannot be built from its options
#include "SnippetSupport.h"


// FlagFraming has no constructor that takes options, so the options value would be dropped.
using Stack = Vub<+[] {
	return VubBuilder()
		.Layer<TestLayer>()
		.Framing<FlagFraming>(7u)
		.Link<VubStreamEnd<64, 16>>()
		.MaxPayload(40)
		.OnReceive<&SnippetReceive>()
		.Clock<&SnippetClock>();
}>;


static_assert(Stack::count == 3);
