#pragma once
#include <Utilities/Checksum/CRC/Crc.h>

namespace Checksum {


// CRC-32Q: aeronautical data (AIXM).
using Crc32Q = Crc<CrcParameters().Width(32).Polynomial(0x814141AB).Initial(0x00000000).FinalXor(0x00000000).Check(0x3010BF7F)>;
}
