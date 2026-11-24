#pragma once
#include <Utilities/Checksum/CRC/Crc.h>

namespace Checksum {


// CRC-16/ISO-IEC-14443-3-A, alias CRC-A: NFC type A tags.
using Crc16IsoIec144433A = Crc<CrcParameters().Width(16).Polynomial(0x1021).Initial(0xC6C6).FinalXor(0x0000).ReflectIn().ReflectOut().Check(0xBF05)>;
}
