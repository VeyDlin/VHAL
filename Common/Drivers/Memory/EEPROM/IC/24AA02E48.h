#pragma once
#include "../Eeprom.h"


namespace EEPROM {

	class _24AA02E48: public Eeprom {
	protected:
		uint16 GetMaxAddressPointer() override {
			return 256;
		}
		AddressSize GetMemoryAddressSize() override {
			return AddressSize::U8;
		}


	public:
		_24AA02E48() {}

		_24AA02E48(AI2C &i2c, bool a0 = false, bool a1 = false, bool a2 = false):
			Eeprom(i2c, 0b10100000 | ((uint8)a0 << 1) | ((uint8)a1 << 2) | ((uint8)a2 << 3))
		{
		}
	};

};
