// expect: a block needs time and the chain has no clock
#include "SnippetSupport.h"
#include <Drivers/Interface/VUB/Framing/Gap.h>


using Stack = VUB::Stack<+[] {
	return VUB::Builder()
		.Framing<VUB::Gap>()
		.GapUs(1750)
		.Link<VUB::StreamEnd<64, 16>>()
		.MaxPayload(40)
		.OnReceive<&SnippetReceive>();
}>;


static_assert(Stack::count == 2);
