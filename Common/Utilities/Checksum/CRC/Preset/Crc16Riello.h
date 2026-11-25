#pragma once
#include <Utilities/Checksum/CRC/Crc.h>

namespace Checksum {


// CRC-16/RIELLO.
using Crc16Riello = Crc<CrcParameters().Width(16).Polynomial(0x1021).Initial(0xB2AA).FinalXor(0x0000).ReflectIn().ReflectOut().Check(0x63D0)>;
}
