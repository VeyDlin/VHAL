#pragma once
#include <Utilities/Checksum/CRC/Crc.h>

namespace Checksum {


// CRC-24/FLEXRAY-B: FlexRay frames.
using Crc24FlexrayB = Crc<CrcParameters().Width(24).Polynomial(0x5D6DCB).Initial(0xABCDEF).FinalXor(0x000000).Check(0x1F23B8)>;
}
