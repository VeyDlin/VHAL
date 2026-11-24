// expect: '4', 'B', '3', '7'
#include <Utilities/Checksum/CRC/Crc.h>

// The same CRC-16/MODBUS with the wrong check value 4B38: the diagnostic shows the value the parameters give, 4B37, in hexadecimal.
inline constexpr Checksum::CrcParameters wrongCheck = Checksum::CrcParameters()
	.Width(16)
	.Polynomial(0x8005)
	.Initial(0xFFFF)
	.ReflectIn()
	.ReflectOut()
	.Check(0x4B38);


using Bad = Checksum::Crc<wrongCheck>;


static_assert(Bad::checked);
