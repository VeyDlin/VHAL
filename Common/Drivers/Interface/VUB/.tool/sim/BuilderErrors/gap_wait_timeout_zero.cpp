// expect: WaitTimeoutUs() needs a timeout above 0
#include "SnippetSupport.h"
#include <Drivers/Interface/VUB/Framing/VubGap.h>


using Stack = Vub<+[] {
	return VubBuilder()
		.Framing<VubGap>()
		.GapUs(1750)
		.WaitTimeoutUs(0)
		.Link<VubStreamEnd<64, 16>>()
		.MaxPayload(40)
		.OnReceive<&SnippetReceive>()
		.Clock<&SnippetClock>();
}>;


static_assert(Stack::count == 2);
