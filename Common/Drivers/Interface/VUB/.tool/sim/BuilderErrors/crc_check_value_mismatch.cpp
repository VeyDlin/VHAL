// expect: the check value of the CRC parameters does not match
#include "SnippetSupport.h"
#include <Drivers/Interface/VUB/Transform/VubCrc.h>


// CRC-16/MODBUS with a check value of 4B38 instead of 4B37: the parameters do not give the catalogue's check value.
inline constexpr VubCrcParameters wrongCheck = {16, 0x8005, 0xFFFF, 0x0000, true, true, 0x4B38};


using Stack = Vub<+[] {
	return VubBuilder()
		.Transform<VubCrc<wrongCheck>>()
		.Link<VubPacketEnd<64, 16>>()
		.MaxPayload(40)
		.OnReceive<&SnippetReceive>()
		.Clock<&SnippetClock>();
}>;


static_assert(Stack::count == 2);
