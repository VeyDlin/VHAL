// expect: OnReceive<&Fn>() is missing
#include "SnippetSupport.h"


using Stack = VUB::Stack<+[] {
	return VUB::Builder()
		.Layer<TestLayer>()
		.Link<VUB::PacketEnd<64, 16>>()
		.MaxPayload(40)
		.Clock<&SnippetClock>();
}>;


static_assert(Stack::count == 2);
