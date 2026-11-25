#pragma once
#include <Utilities/Checksum/CRC/Crc.h>

namespace Checksum {


// CRC-12/UMTS, alias CRC-12/3GPP: UMTS mobile telephony.
using Crc12Umts = Crc<CrcParameters().Width(12).Polynomial(0x80F).Initial(0x000).FinalXor(0x000).ReflectOut().Check(0xDAF)>;
}
