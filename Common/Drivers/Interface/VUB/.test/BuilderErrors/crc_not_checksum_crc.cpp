// expect: VUB::Crc takes a CRC type of the shared CRC library
#include "SnippetSupport.h"
#include <Drivers/Interface/VUB/Transform/Crc.h>


// A type that is not a Checksum::Crc: the transform refuses it, and the message names the type to pass.
struct NotCrc {
};


using Stack = VUB::Stack<+[] {
	return VUB::Builder()
		.Transform<VUB::Crc<NotCrc>>()
		.Link<VUB::PacketEnd<64, 16>>()
		.MaxPayload(40)
		.OnReceive<&SnippetReceive>()
		.Clock<&SnippetClock>();
}>;


static_assert(Stack::count == 2);
