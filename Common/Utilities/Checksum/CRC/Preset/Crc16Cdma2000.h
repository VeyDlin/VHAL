#pragma once
#include <Utilities/Checksum/CRC/Crc.h>

namespace Checksum {


// CRC-16/CDMA2000: CDMA2000 air interface.
using Crc16Cdma2000 = Crc<CrcParameters().Width(16).Polynomial(0xC867).Initial(0xFFFF).FinalXor(0x0000).Check(0x4C06)>;
}
