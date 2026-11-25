#pragma once
#include <Utilities/Checksum/CRC/Crc.h>

namespace Checksum {


// CRC-32/ISO-HDLC, aliases CRC-32 and PKZIP: Ethernet frames and PKZIP archives.
using Crc32IsoHdlc = Crc<CrcParameters().Width(32).Polynomial(0x04C11DB7).Initial(0xFFFFFFFF).FinalXor(0xFFFFFFFF).ReflectIn().ReflectOut().Check(0xCBF43926)>;
}
