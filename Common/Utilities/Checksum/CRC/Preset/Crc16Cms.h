#pragma once
#include <Utilities/Checksum/CRC/Crc.h>

namespace Checksum {


// CRC-16/CMS.
using Crc16Cms = Crc<CrcParameters().Width(16).Polynomial(0x8005).Initial(0xFFFF).FinalXor(0x0000).Check(0xAEE7)>;
}
