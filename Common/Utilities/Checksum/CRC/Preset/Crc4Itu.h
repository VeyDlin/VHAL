#pragma once
#include <Utilities/Checksum/CRC/Crc.h>

namespace Checksum {


// CRC-4/ITU, alias CRC-4/G-704: ITU-T G.704 multiframes.
using Crc4Itu = Crc<CrcParameters().Width(4).Polynomial(0x3).Initial(0x0).FinalXor(0x0).ReflectIn().ReflectOut().Check(0x7)>;
}
