#pragma once
#include <Utilities/Checksum/CRC/Crc.h>

namespace Checksum {


// CRC-21/CAN-FD: CAN FD frames.
using Crc21Can = Crc<CrcParameters().Width(21).Polynomial(0x102899).Initial(0x000000).FinalXor(0x000000).Check(0x0ED841)>;
}
