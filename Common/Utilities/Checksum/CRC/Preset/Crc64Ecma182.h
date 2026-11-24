#pragma once
#include <Utilities/Checksum/CRC/Crc.h>

namespace Checksum {


// CRC-64/ECMA-182.
using Crc64Ecma182 = Crc<CrcParameters().Width(64).Polynomial(0x42F0E1EBA9EA3693).Initial(0x0000000000000000).FinalXor(0x0000000000000000).Check(0x6C40DF5F0B497347)>;
}
