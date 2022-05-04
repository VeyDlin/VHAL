#pragma once
#include "../Eeprom.h"


namespace EEPROM {

	class AT24C256C: public Eeprom  {
	protected:
		uint16 GetMaxAddressPointer() override {
			return 32 * 1024;
		}
		AddressSize GetMemoryAddressSize() override {
			return AddressSize::U16;
		}


	public:
		AT24C256C() {}

		AT24C256C(AI2C &i2c, bool a0 = false, bool a1 = false, bool a2 = false):
			Eeprom(i2c, 0b10100000 | ((uint8)a0 << 1) | ((uint8)a1 << 2) | ((uint8)a2 << 3))
		{
		}
	};

};
