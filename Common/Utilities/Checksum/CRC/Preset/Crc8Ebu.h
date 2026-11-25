#pragma once
#include <Utilities/Checksum/CRC/Crc.h>

namespace Checksum {


// CRC-8/EBU.
using Crc8Ebu = Crc<CrcParameters().Width(8).Polynomial(0x1D).Initial(0xFF).FinalXor(0x00).ReflectIn().ReflectOut().Check(0x97)>;
}
