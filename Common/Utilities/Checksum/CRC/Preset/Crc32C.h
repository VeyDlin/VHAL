#pragma once
#include <Utilities/Checksum/CRC/Crc.h>

namespace Checksum {


// CRC-32C, aliases CRC-32/ISCSI and CRC-32/CASTAGNOLI: iSCSI.
using Crc32C = Crc<CrcParameters().Width(32).Polynomial(0x1EDC6F41).Initial(0xFFFFFFFF).FinalXor(0xFFFFFFFF).ReflectIn().ReflectOut().Check(0xE3069283)>;
}
