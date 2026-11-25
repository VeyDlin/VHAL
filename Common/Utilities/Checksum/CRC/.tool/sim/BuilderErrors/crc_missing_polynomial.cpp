// expect: the CRC parameters have no Polynomial
#include <Utilities/Checksum/CRC/Crc.h>

// CRC-16/MODBUS without its Polynomial: the width is given, so the chain names the polynomial that is missing.
inline constexpr Checksum::CrcParameters noPolynomial = Checksum::CrcParameters()
	.Width(16)
	.Initial(0xFFFF)
	.ReflectIn()
	.ReflectOut()
	.Check(0x4B37);


using Bad = Checksum::Crc<noPolynomial>;


static_assert(Bad::checked);
