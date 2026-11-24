#pragma once
#include <Utilities/Checksum/CRC/Crc.h>

namespace Checksum {


// CRC-17/CAN-FD: CAN FD frames.
using Crc17Can = Crc<CrcParameters().Width(17).Polynomial(0x1685B).Initial(0x00000).FinalXor(0x00000).Check(0x04F03)>;
}
