#pragma once
#include <Utilities/Checksum/CRC/Crc.h>

namespace Checksum {


// CRC-16/ARC, aliases CRC-IBM, CRC-16 and CRC-16/LHA.
using Crc16Arc = Crc<CrcParameters().Width(16).Polynomial(0x8005).Initial(0x0000).FinalXor(0x0000).ReflectIn().ReflectOut().Check(0xBB3D)>;
}
