#pragma once
#include <Utilities/Checksum/CRC/Crc.h>

namespace Checksum {


// CRC-8/WCDMA.
using Crc8Wcdma = Crc<CrcParameters().Width(8).Polynomial(0x9B).Initial(0x00).FinalXor(0x00).ReflectIn().ReflectOut().Check(0x25)>;
}
