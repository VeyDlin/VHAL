#pragma once
#include "../Eeprom.h"
#include "../Data.h"


namespace EEPROM {


	class AT24C256C: public Eeprom  {
	protected:
		uint16 GetMaxAddressPointer() override {
			return 32 * 1024; // Память 32 килобайта
		}
		AddressSize GetMemoryAddressSize() override {
			return AddressSize::u16;
		}


	public:
		AT24C256C() {}

		AT24C256C(I2C_HandleTypeDef &i2c, bool a0 = false, bool a1 = false, bool a2 = false) {
			SetI2c(i2c);
			SetAddress(0b10100000 | ((uint8)a0 << 1) | ((uint8)a1 << 2) | ((uint8)a2 << 3));
		}
	};


};
