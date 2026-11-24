#pragma once
#include <Utilities/Checksum/CRC/Crc.h>

namespace Checksum {


// CRC-10/CDMA2000.
using Crc10Cdma2000 = Crc<CrcParameters().Width(10).Polynomial(0x3D9).Initial(0x3FF).FinalXor(0x000).Check(0x233)>;
}
