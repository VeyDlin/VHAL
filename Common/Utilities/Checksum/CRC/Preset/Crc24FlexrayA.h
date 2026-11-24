#pragma once
#include <Utilities/Checksum/CRC/Crc.h>

namespace Checksum {


// CRC-24/FLEXRAY-A: FlexRay frames.
using Crc24FlexrayA = Crc<CrcParameters().Width(24).Polynomial(0x5D6DCB).Initial(0xFEDCBA).FinalXor(0x000000).Check(0x7979BD)>;
}
