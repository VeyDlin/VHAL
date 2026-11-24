// expect: the polynomial of the CRC parameters is wider than the width
#include <Utilities/Checksum/CRC/Crc.h>

// CRC-16 with a 17-bit polynomial: the leading term is not in the normal form, so the polynomial is wider than the width.
inline constexpr Checksum::CrcParameters wideTerm = Checksum::CrcParameters()
	.Width(16)
	.Polynomial(0x18005)
	.ReflectIn()
	.ReflectOut();


using Bad = Checksum::Crc<wideTerm>;


static_assert(Bad::checked);
