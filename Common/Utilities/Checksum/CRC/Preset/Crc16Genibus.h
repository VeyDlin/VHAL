#pragma once
#include <Utilities/Checksum/CRC/Crc.h>

namespace Checksum {


// CRC-16/GENIBUS, aliases CRC-16/EPC and CRC-16/I-CODE.
using Crc16Genibus = Crc<CrcParameters().Width(16).Polynomial(0x1021).Initial(0xFFFF).FinalXor(0xFFFF).Check(0xD64E)>;
}
