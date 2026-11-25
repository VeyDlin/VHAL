#pragma once
#include <Utilities/Checksum/CRC/Crc.h>

namespace Checksum {


// CRC-16/DNP: DNP3 links.
using Crc16Dnp = Crc<CrcParameters().Width(16).Polynomial(0x3D65).Initial(0x0000).FinalXor(0xFFFF).ReflectIn().ReflectOut().Check(0xEA82)>;
}
