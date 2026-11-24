#pragma once
#include <Utilities/Checksum/CRC/Crc.h>

namespace Checksum {


// CRC-16/X-25: X.25 and HDLC frames.
using Crc16X25 = Crc<CrcParameters().Width(16).Polynomial(0x1021).Initial(0xFFFF).FinalXor(0xFFFF).ReflectIn().ReflectOut().Check(0x906E)>;
}
