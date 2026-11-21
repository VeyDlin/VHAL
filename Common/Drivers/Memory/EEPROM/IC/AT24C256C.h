#pragma once
#include "../Eeprom.h"


namespace EEPROM {
	// 256 Kbit serial EEPROM: 32768 bytes in 64-byte pages with a two-byte word address. The control byte is 1010 A2 A1 A0 R/W,
	// so the 7-bit device address is 0x50 with the A2:A0 pins in bits 2:0. The write cycle takes up to 5 ms.
	class AT24C256C: public Eeprom {
	public:
		static constexpr Geometry chipGeometry{
			.capacity = 32768,
			.pageSize = 64,
			.addressBytes = 2,
			.writeCycleMs = 5
		};


		AT24C256C(AI2C &i2cBus, bool a0 = false, bool a1 = false, bool a2 = false):
			Eeprom(i2cBus, static_cast<uint8>(0x50 | (a2 << 2) | (a1 << 1) | a0), chipGeometry)
		{

		}
	};
};
