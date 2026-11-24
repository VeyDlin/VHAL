#pragma once
#include <Utilities/Checksum/CRC/Crc.h>

namespace Checksum {


// CRC-5/ITU.
using Crc5Itu = Crc<CrcParameters().Width(5).Polynomial(0x15).Initial(0x00).FinalXor(0x00).ReflectIn().ReflectOut().Check(0x07)>;
}
