#pragma once
#include <Utilities/Checksum/CRC/Crc.h>

namespace Checksum {


// CRC-16/TMS37157.
using Crc16Tms37157 = Crc<CrcParameters().Width(16).Polynomial(0x1021).Initial(0x89EC).FinalXor(0x0000).ReflectIn().ReflectOut().Check(0x26B1)>;
}
