#pragma once
#include <Utilities/Checksum/CRC/Crc.h>

namespace Checksum {


// CRC-5/EPC: EPC Gen2 RFID tags.
using Crc5Epc = Crc<CrcParameters().Width(5).Polynomial(0x09).Initial(0x09).FinalXor(0x00).Check(0x00)>;
}
