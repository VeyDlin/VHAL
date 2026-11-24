#pragma once
#include <Utilities/Checksum/CRC/Crc.h>

namespace Checksum {


// CRC-6/CDMA2000-B.
using Crc6Cdma2000B = Crc<CrcParameters().Width(6).Polynomial(0x07).Initial(0x3F).FinalXor(0x00).Check(0x3B)>;
}
