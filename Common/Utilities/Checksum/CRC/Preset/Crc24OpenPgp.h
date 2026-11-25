#pragma once
#include <Utilities/Checksum/CRC/Crc.h>

namespace Checksum {


// CRC-24/OPENPGP, alias CRC-24: OpenPGP armour checksum.
using Crc24OpenPgp = Crc<CrcParameters().Width(24).Polynomial(0x864CFB).Initial(0xB704CE).FinalXor(0x000000).Check(0x21CF02)>;
}
