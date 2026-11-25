#pragma once
#include <Utilities/Checksum/CRC/Crc.h>

namespace Checksum {


// CRC-12/CDMA2000.
using Crc12Cdma2000 = Crc<CrcParameters().Width(12).Polynomial(0xF13).Initial(0xFFF).FinalXor(0x000).Check(0xD4D)>;
}
