#pragma once
#include <Utilities/Checksum/CRC/Crc.h>

namespace Checksum {


// CRC-32/POSIX, alias CKSUM: the POSIX cksum utility.
using Crc32Posix = Crc<CrcParameters().Width(32).Polynomial(0x04C11DB7).Initial(0x00000000).FinalXor(0xFFFFFFFF).Check(0x765E7680)>;
}
