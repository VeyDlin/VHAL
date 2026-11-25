#pragma once
#include <Utilities/Checksum/CRC/Crc.h>

namespace Checksum {


// CRC-15/CAN: CAN bus frames.
using Crc15Can = Crc<CrcParameters().Width(15).Polynomial(0x4599).Initial(0x0000).FinalXor(0x0000).Check(0x059E)>;
}
