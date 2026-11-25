#pragma once
#include <Utilities/Checksum/CRC/Crc.h>

namespace Checksum {


// CRC-40/GSM.
using Crc40Gsm = Crc<CrcParameters().Width(40).Polynomial(0x0004820009).Initial(0x0000000000).FinalXor(0xFFFFFFFFFF).Check(0xD4164FC646)>;
}
