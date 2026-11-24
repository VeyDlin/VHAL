#pragma once
#include <Utilities/Checksum/CRC/Crc.h>

namespace Checksum {


// CRC-16/CCITT-FALSE.
using Crc16CcittFalse = Crc<CrcParameters().Width(16).Polynomial(0x1021).Initial(0xFFFF).FinalXor(0x0000).Check(0x29B1)>;
}
