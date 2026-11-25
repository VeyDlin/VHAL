#pragma once
#include <Utilities/Checksum/CRC/Crc.h>

namespace Checksum {


// CRC-16/USB: USB data packets.
using Crc16Usb = Crc<CrcParameters().Width(16).Polynomial(0x8005).Initial(0xFFFF).FinalXor(0xFFFF).ReflectIn().ReflectOut().Check(0xB4C8)>;
}
