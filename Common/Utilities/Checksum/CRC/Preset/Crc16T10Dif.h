#pragma once
#include <Utilities/Checksum/CRC/Crc.h>

namespace Checksum {


// CRC-16/T10-DIF: SCSI T10 data integrity fields.
using Crc16T10Dif = Crc<CrcParameters().Width(16).Polynomial(0x8BB7).Initial(0x0000).FinalXor(0x0000).Check(0xD0DB)>;
}
