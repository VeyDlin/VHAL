#pragma once
#include <Utilities/Checksum/CRC/Crc.h>

namespace Checksum {


// CRC-16/MODBUS: Modbus RTU.
using Crc16Modbus = Crc<CrcParameters().Width(16).Polynomial(0x8005).Initial(0xFFFF).FinalXor(0x0000).ReflectIn().ReflectOut().Check(0x4B37)>;
}
