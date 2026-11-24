#pragma once
#include <Utilities/Checksum/CRC/Crc.h>

namespace Checksum {


// CRC-16/KERMIT: Kermit file transfer.
using Crc16Kermit = Crc<CrcParameters().Width(16).Polynomial(0x1021).Initial(0x0000).FinalXor(0x0000).ReflectIn().ReflectOut().Check(0x2189)>;
}
