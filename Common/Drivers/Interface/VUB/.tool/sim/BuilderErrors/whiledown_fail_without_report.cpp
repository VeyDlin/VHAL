// expect: WhileDown(VubDown::Fail) needs
#include "SnippetSupport.h"


// Nothing in this chain can report the link down: the link has no ReportsState() and no layer reports its peer.
using Stack = Vub<+[] {
	return VubBuilder()
		.Layer<TestLayer>()
		.Link<VubPacketEnd<64, 16>>()
		.MaxPayload(40)
		.WhileDown(VubDown::Fail)
		.OnReceive<&SnippetReceive>()
		.Clock<&SnippetClock>();
}>;


static_assert(Stack::count == 2);
