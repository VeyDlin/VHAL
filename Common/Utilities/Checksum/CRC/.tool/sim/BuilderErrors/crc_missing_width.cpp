// expect: the CRC parameters have no Width
#include <Utilities/Checksum/CRC/Crc.h>

// CRC-16/MODBUS without its Width: the chain names the method that is missing.
inline constexpr Checksum::CrcParameters noWidth = Checksum::CrcParameters()
	.Polynomial(0x8005)
	.Initial(0xFFFF)
	.ReflectIn()
	.ReflectOut()
	.Check(0x4B37);


using Bad = Checksum::Crc<noWidth>;


static_assert(Bad::checked);
