#pragma once
#include <Utilities/Checksum/CRC/Crc.h>

namespace Checksum {


// CRC-6/ITU, alias CRC-6/G-704: ITU-T G.704 multiframes.
using Crc6Itu = Crc<CrcParameters().Width(6).Polynomial(0x03).Initial(0x00).FinalXor(0x00).ReflectIn().ReflectOut().Check(0x06)>;
}
