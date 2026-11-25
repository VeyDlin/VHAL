#pragma once
#include <Utilities/Checksum/CRC/Crc.h>

namespace Checksum {


// CRC-32/MPEG-2: MPEG-2 transport stream sections.
using Crc32Mpeg2 = Crc<CrcParameters().Width(32).Polynomial(0x04C11DB7).Initial(0xFFFFFFFF).FinalXor(0x00000000).Check(0x0376E6E7)>;
}
