// expect: a one byte length holds packets of 255 bytes at most
#include "SnippetSupport.h"
#include <Drivers/Interface/VUB/Framing/LengthPrefix.h>


using Stack = VUB::Stack<+[] {
	return VUB::Builder()
		.Framing<VUB::LengthPrefix<1>>()
		.Link<VUB::StreamEnd<64, 16>>()
		.MaxPayload(300)
		.OnReceive<&SnippetReceive>()
		.Clock<&SnippetClock>();
}>;


static_assert(Stack::count == 2);
