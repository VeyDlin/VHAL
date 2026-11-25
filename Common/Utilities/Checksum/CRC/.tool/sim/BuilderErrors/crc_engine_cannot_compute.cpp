// expect: the engine cannot compute this CRC
#include <Utilities/Checksum/CRC/Crc.h>
#include <Utilities/Checksum/CRC/Engine/CrcTable.h>

// A 4-bit CRC with the table engine: the table engine takes widths of 8 bits and above, so its Supports says no.
inline constexpr Checksum::CrcParameters narrow = Checksum::CrcParameters()
	.Width(4)
	.Polynomial(0x3);


using Bad = Checksum::Crc<narrow, Checksum::CrcTable>;


static_assert(Bad::checked);
