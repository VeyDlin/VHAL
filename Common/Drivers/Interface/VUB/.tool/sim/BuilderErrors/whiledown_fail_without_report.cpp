// expect: WhileDown(VUB::Down::Fail) needs
#include "SnippetSupport.h"


// Nothing in this chain can report the link down: the link has no ReportsState() and no layer reports its peer.
using Stack = VUB::Stack<+[] {
	return VUB::Builder()
		.Layer<TestLayer>()
		.Link<VUB::PacketEnd<64, 16>>()
		.MaxPayload(40)
		.WhileDown(VUB::Down::Fail)
		.OnReceive<&SnippetReceive>()
		.Clock<&SnippetClock>();
}>;


static_assert(Stack::count == 2);
