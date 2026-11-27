// expect: the check value of the CRC parameters does not match the CRC of 123456789
#include <Utilities/Checksum/CRC/Crc.h>

// CRC-16/MODBUS with a check value of 4B38 instead of 4B37: the parameters do not give the catalogue's check value.
inline constexpr Checksum::CrcParameters wrongCheck = Checksum::CrcParameters()
	.Width(16)
	.Polynomial(0x8005)
	.Initial(0xFFFF)
	.ReflectIn()
	.ReflectOut()
	.Check(0x4B38);


using Bad = Checksum::Crc<wrongCheck>;


static_assert(Bad::checked);
