// expect: has no member named 'Second'
#include "SnippetSupport.h"


// MaxPayload is a plain builder method: after it the block's option methods are gone.
using Stack = Vub<+[] {
	return VubBuilder()
		.Layer<StepLayer>()
		.MaxPayload(40)
		.Second(3)
		.Link<VubPacketEnd<64, 16>>()
		.OnReceive<&SnippetReceive>()
		.Clock<&SnippetClock>();
}>;


static_assert(Stack::count == 2);
