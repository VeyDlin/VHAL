// expect: a CRC is 1 to 64 bits wide
#include <Utilities/Checksum/CRC/Crc.h>

// A 65-bit CRC: the width is outside 1 to 64, and the message says so.
inline constexpr Checksum::CrcParameters tooWide = Checksum::CrcParameters()
	.Width(65)
	.Polynomial(0x1B);


using Bad = Checksum::Crc<tooWide>;


static_assert(Bad::checked);
