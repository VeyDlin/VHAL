// expect: WaitTimeoutUs() needs a timeout above 0
#include "SnippetSupport.h"
#include <Drivers/Interface/VUB/Framing/Gap.h>


using Stack = VUB::Stack<+[] {
	return VUB::Builder()
		.Framing<VUB::Gap>()
		.GapUs(1750)
		.WaitTimeoutUs(0)
		.Link<VUB::StreamEnd<64, 16>>()
		.MaxPayload(40)
		.OnReceive<&SnippetReceive>()
		.Clock<&SnippetClock>();
}>;


static_assert(Stack::count == 2);
