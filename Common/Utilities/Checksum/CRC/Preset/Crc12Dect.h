#pragma once
#include <Utilities/Checksum/CRC/Crc.h>

namespace Checksum {


// CRC-12/DECT: DECT cordless telephony.
using Crc12Dect = Crc<CrcParameters().Width(12).Polynomial(0x80F).Initial(0x000).FinalXor(0x000).Check(0xF5B)>;
}
