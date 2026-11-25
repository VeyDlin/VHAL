#pragma once
#include <Utilities/Checksum/CRC/Crc.h>

namespace Checksum {


// CRC-15/MPT1327: MPT 1327 trunked radio.
using Crc15Mpt1327 = Crc<CrcParameters().Width(15).Polynomial(0x6815).Initial(0x0000).FinalXor(0x0001).Check(0x2566)>;
}
