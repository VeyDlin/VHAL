// expect: a one byte length holds packets of 255 bytes at most
#include "SnippetSupport.h"
#include <Drivers/Interface/VUB/Framing/VubLengthPrefix.h>


using Stack = Vub<+[] {
	return VubBuilder()
		.Framing<VubLengthPrefix<1>>()
		.Link<VubStreamEnd<64, 16>>()
		.MaxPayload(300)
		.OnReceive<&SnippetReceive>()
		.Clock<&SnippetClock>();
}>;


static_assert(Stack::count == 2);
