// expect: a CRC is 8, 16 or 32 bits wide
#include "SnippetSupport.h"
#include <Drivers/Interface/VUB/Transform/VubCrc.h>


// CRC-12/UMTS-like parameters: the width 12 is not one the transform takes.
inline constexpr VubCrcParameters twelveBits = {12, 0x80F, 0x000, 0x000, false, true, 0x000};


using Stack = Vub<+[] {
	return VubBuilder()
		.Transform<VubCrc<twelveBits>>()
		.Link<VubPacketEnd<64, 16>>()
		.MaxPayload(40)
		.OnReceive<&SnippetReceive>()
		.Clock<&SnippetClock>();
}>;


static_assert(Stack::count == 2);
