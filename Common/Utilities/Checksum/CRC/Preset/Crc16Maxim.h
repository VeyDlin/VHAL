#pragma once
#include <Utilities/Checksum/CRC/Crc.h>

namespace Checksum {


// CRC-16/MAXIM-DOW: Dallas/Maxim 1-Wire devices.
using Crc16Maxim = Crc<CrcParameters().Width(16).Polynomial(0x8005).Initial(0x0000).FinalXor(0xFFFF).ReflectIn().ReflectOut().Check(0x44C2)>;
}
