// expect: LayerOutbox is for a stack that is not concurrent
#include "SnippetSupport.h"


using Stack = VUB::Stack<+[] {
	return VUB::Builder()
		.Layer<TestLayer>()
		.Link<VUB::PacketEnd<64, 16>>()
		.MaxPayload(40)
		.Concurrent()
		.OnReceive<&SnippetReceive>()
		.Clock<&SnippetClock>();
}>;


// Taking the address of the member template instantiates it: the outbox of a concurrent stack is refused.
inline auto snippetOutbox = &Stack::template LayerOutbox<0>;
