#pragma once
#include <Utilities/Checksum/CRC/Crc.h>

namespace Checksum {


// CRC-10.
using Crc10 = Crc<CrcParameters().Width(10).Polynomial(0x233).Initial(0x000).FinalXor(0x000).Check(0x199)>;
}
