#pragma once
#include <Utilities/Checksum/CRC/Crc.h>

namespace Checksum {


// CRC-8/SMBUS, alias CRC-8: SMBus packet error checking.
using Crc8Smbus = Crc<CrcParameters().Width(8).Polynomial(0x07).Initial(0x00).FinalXor(0x00).Check(0xF4)>;
}
