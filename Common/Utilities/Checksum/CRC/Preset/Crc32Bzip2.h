#pragma once
#include <Utilities/Checksum/CRC/Crc.h>

namespace Checksum {


// CRC-32/BZIP2, aliases CRC-32/AAL5 and CRC-32/DECT-B: bzip2 files.
using Crc32Bzip2 = Crc<CrcParameters().Width(32).Polynomial(0x04C11DB7).Initial(0xFFFFFFFF).FinalXor(0xFFFFFFFF).Check(0xFC891918)>;
}
