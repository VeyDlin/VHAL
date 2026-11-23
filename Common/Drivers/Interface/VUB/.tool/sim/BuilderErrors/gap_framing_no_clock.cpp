// expect: a block needs time and the chain has no clock
#include "SnippetSupport.h"
#include <Drivers/Interface/VUB/Framing/VubGap.h>


using Stack = Vub<+[] {
	return VubBuilder()
		.Framing<VubGap>()
		.GapUs(1750)
		.Link<VubStreamEnd<64, 16>>()
		.MaxPayload(40)
		.OnReceive<&SnippetReceive>();
}>;


static_assert(Stack::count == 2);
