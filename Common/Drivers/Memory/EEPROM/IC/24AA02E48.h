#pragma once
#include "../Eeprom.h"


namespace EEPROM {
	// 2 Kbit serial EEPROM: 256 bytes in 8-byte pages with a one-byte word address. The A0 to A2 pins are not connected, so the
	// 7-bit device address is always 0x50. The write cycle takes up to 5 ms. The EUI-48 node address is not modelled here.
	class Chip24AA02E48: public Eeprom {
	public:
		static constexpr Geometry chipGeometry{
			.capacity = 256,
			.pageSize = 8,
			.addressBytes = 1,
			.writeCycleMs = 5
		};


		Chip24AA02E48(AI2C &i2cBus):
			Eeprom(i2cBus, 0x50, chipGeometry)
		{

		}
	};
};
