// expect: 'E', '5', 'C', 'C'
#include "SnippetSupport.h"
#include <Drivers/Interface/VUB/Transform/Crc.h>


// CRC-16/SPI-FUJITSU with the check value E5CD instead of E5CC: the error shows the value the parameters give, E5CC, in hexadecimal.
inline constexpr Checksum::CrcParameters wrongCheck = Checksum::CrcParameters()
	.Width(16)
	.Polynomial(0x1021)
	.Initial(0x1D0F)
	.FinalXor(0x0000)
	.ReflectIn(false)
	.ReflectOut(false)
	.Check(0xE5CD);


using Stack = VUB::Stack<+[] {
	return VUB::Builder()
		.Transform<VUB::Crc<Checksum::Crc<wrongCheck>>>()
		.Link<VUB::PacketEnd<64, 16>>()
		.MaxPayload(40)
		.OnReceive<&SnippetReceive>()
		.Clock<&SnippetClock>();
}>;


static_assert(Stack::count == 2);
