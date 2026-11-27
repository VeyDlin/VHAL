#pragma once
#include <Utilities/Checksum/CRC/CrcParameters.h>
#include <Utilities/DataTypes.h>

// The three algorithms of the size table: CRC-16/MODBUS, CRC-32/ISO-HDLC and CRC-8/SMBUS. Each size program uses one or all three.
namespace SizeAlgorithms {


constexpr Checksum::CrcParameters Modbus = Checksum::CrcParameters()
	.Width(16)
	.Polynomial(0x8005)
	.Initial(0xFFFF)
	.ReflectIn()
	.ReflectOut();


constexpr Checksum::CrcParameters IsoHdlc = Checksum::CrcParameters()
	.Width(32)
	.Polynomial(0x04C11DB7)
	.Initial(0xFFFFFFFF)
	.FinalXor(0xFFFFFFFF)
	.ReflectIn()
	.ReflectOut();


constexpr Checksum::CrcParameters Smbus = Checksum::CrcParameters()
	.Width(8)
	.Polynomial(0x07);
}
