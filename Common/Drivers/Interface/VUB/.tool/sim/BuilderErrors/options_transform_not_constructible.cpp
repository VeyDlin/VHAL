// expect: the transform cannot be built from its options
#include "SnippetSupport.h"


// XorTransform has no constructor that takes options, so the options value would be dropped.
using Stack = Vub<+[] {
	return VubBuilder()
		.Layer<TestLayer>()
		.Transform<XorTransform>(7u)
		.Link<VubPacketEnd<64, 16>>()
		.MaxPayload(40)
		.OnReceive<&SnippetReceive>()
		.Clock<&SnippetClock>();
}>;


static_assert(Stack::count == 3);
