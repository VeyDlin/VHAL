#pragma once
#include <Utilities/Checksum/CRC/Crc.h>

namespace Checksum {


// CRC-11.
using Crc11 = Crc<CrcParameters().Width(11).Polynomial(0x385).Initial(0x01A).FinalXor(0x000).Check(0x5A3)>;
}
