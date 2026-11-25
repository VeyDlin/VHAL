#pragma once
#include <Utilities/Checksum/CRC/Crc.h>

namespace Checksum {


// CRC-16/DECT-X, alias X-CRC-16: DECT radio.
using Crc16DectX = Crc<CrcParameters().Width(16).Polynomial(0x0589).Initial(0x0000).FinalXor(0x0000).Check(0x007F)>;
}
