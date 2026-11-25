#pragma once
#include <Utilities/Checksum/CRC/Crc.h>

namespace Checksum {


// CRC-8/MAXIM-DOW, alias CRC-8/MAXIM: Dallas/Maxim 1-Wire devices.
using Crc8Maxim = Crc<CrcParameters().Width(8).Polynomial(0x31).Initial(0x00).FinalXor(0x00).ReflectIn().ReflectOut().Check(0xA1)>;
}
