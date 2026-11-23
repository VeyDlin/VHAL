// expect: the check value of the CRC parameters does not match
#include "SnippetSupport.h"
#include <Drivers/Interface/VUB/Transform/Crc.h>


// CRC-16/MODBUS with a check value of 4B38 instead of 4B37: the parameters do not give the catalogue's check value.
inline constexpr VUB::CrcParameters wrongCheck = VUB::CrcParameters()
	.Width(16)
	.Polynomial(0x8005)
	.Initial(0xFFFF)
	.FinalXor(0x0000)
	.ReflectIn()
	.ReflectOut()
	.Check(0x4B38);


using Stack = VUB::Stack<+[] {
	return VUB::Builder()
		.Transform<VUB::Crc<wrongCheck>>()
		.Link<VUB::PacketEnd<64, 16>>()
		.MaxPayload(40)
		.OnReceive<&SnippetReceive>()
		.Clock<&SnippetClock>();
}>;


static_assert(Stack::count == 2);
