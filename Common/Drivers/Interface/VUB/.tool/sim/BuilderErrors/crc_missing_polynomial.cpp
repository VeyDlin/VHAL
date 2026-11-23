// expect: the CRC parameters have no Polynomial
#include "SnippetSupport.h"
#include <Drivers/Interface/VUB/Transform/Crc.h>


// CRC-16/SPI-FUJITSU without its Polynomial: the chain names the method that is missing.
inline constexpr VUB::CrcParameters noPolynomial = VUB::CrcParameters()
	.Width(16)
	.Initial(0x1D0F)
	.FinalXor(0x0000)
	.ReflectIn(false)
	.ReflectOut(false)
	.Check(0xE5CC);


using Stack = VUB::Stack<+[] {
	return VUB::Builder()
		.Transform<VUB::Crc<noPolynomial>>()
		.Link<VUB::PacketEnd<64, 16>>()
		.MaxPayload(40)
		.OnReceive<&SnippetReceive>()
		.Clock<&SnippetClock>();
}>;


static_assert(Stack::count == 2);
