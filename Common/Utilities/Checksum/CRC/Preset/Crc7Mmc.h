#pragma once
#include <Utilities/Checksum/CRC/Crc.h>

namespace Checksum {


// CRC-7/MMC, alias CRC-7: MultiMediaCard commands.
using Crc7Mmc = Crc<CrcParameters().Width(7).Polynomial(0x09).Initial(0x00).FinalXor(0x00).Check(0x75)>;
}
