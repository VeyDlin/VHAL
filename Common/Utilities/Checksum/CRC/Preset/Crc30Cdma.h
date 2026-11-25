#pragma once
#include <Utilities/Checksum/CRC/Crc.h>

namespace Checksum {


// CRC-30/CDMA.
using Crc30Cdma = Crc<CrcParameters().Width(30).Polynomial(0x2030B9C7).Initial(0x3FFFFFFF).FinalXor(0x3FFFFFFF).Check(0x04C34ABF)>;
}
