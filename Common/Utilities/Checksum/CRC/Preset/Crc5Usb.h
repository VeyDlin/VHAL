#pragma once
#include <Utilities/Checksum/CRC/Crc.h>

namespace Checksum {


// CRC-5/USB: USB token packets.
using Crc5Usb = Crc<CrcParameters().Width(5).Polynomial(0x05).Initial(0x1F).FinalXor(0x1F).ReflectIn().ReflectOut().Check(0x19)>;
}
