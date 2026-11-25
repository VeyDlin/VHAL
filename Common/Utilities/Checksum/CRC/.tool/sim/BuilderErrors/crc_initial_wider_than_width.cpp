// expect: the initial value of the CRC parameters is wider than the width
#include <Utilities/Checksum/CRC/Crc.h>

// CRC-8 with an initial value of 0x1FF: the initial value does not fit in 8 bits.
inline constexpr Checksum::CrcParameters wideInitial = Checksum::CrcParameters()
	.Width(8)
	.Polynomial(0x07)
	.Initial(0x1FF);


using Bad = Checksum::Crc<wideInitial>;


static_assert(Bad::checked);
