#pragma once
#include <Utilities/Checksum/CRC/Crc.h>

namespace Checksum {


// CRC-16/XMODEM, aliases CRC-16/ZMODEM, CRC-16/ACORN and CRC-16/LTE: XMODEM file transfer.
using Crc16Xmodem = Crc<CrcParameters().Width(16).Polynomial(0x1021).Initial(0x0000).FinalXor(0x0000).Check(0x31C3)>;
}
