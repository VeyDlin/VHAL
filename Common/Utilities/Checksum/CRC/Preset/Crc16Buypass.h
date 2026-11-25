#pragma once
#include <Utilities/Checksum/CRC/Crc.h>

namespace Checksum {


// CRC-16/BUYPASS, alias CRC-16/VERIFONE.
using Crc16Buypass = Crc<CrcParameters().Width(16).Polynomial(0x8005).Initial(0x0000).FinalXor(0x0000).Check(0xFEE8)>;
}
