#pragma once
#include <Utilities/Checksum/CRC/Crc.h>

namespace Checksum {


// CRC-13/BBC.
using Crc13Bbc = Crc<CrcParameters().Width(13).Polynomial(0x1CF5).Initial(0x0000).FinalXor(0x0000).Check(0x04FA)>;
}
