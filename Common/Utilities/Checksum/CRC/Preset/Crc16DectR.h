#pragma once
#include <Utilities/Checksum/CRC/Crc.h>

namespace Checksum {


// CRC-16/DECT-R, alias R-CRC-16: DECT radio.
using Crc16DectR = Crc<CrcParameters().Width(16).Polynomial(0x0589).Initial(0x0000).FinalXor(0x0001).Check(0x007E)>;
}
