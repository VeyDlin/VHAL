// expect: the final XOR of the CRC parameters is wider than the width
#include <Utilities/Checksum/CRC/Crc.h>

// CRC-12 with a final XOR of 0x1000: the final XOR does not fit in 12 bits.
inline constexpr Checksum::CrcParameters wideXor = Checksum::CrcParameters()
	.Width(12)
	.Polynomial(0x80F)
	.FinalXor(0x1000);


using Bad = Checksum::Crc<wideXor>;


static_assert(Bad::checked);
