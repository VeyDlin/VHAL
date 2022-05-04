#pragma once
#include "../Eeprom.h"


class _24AA02E48: public Eeprom {
protected:
	uint16 GetMaxAddressPointer() override { 
		return 256; // Память 256 байт
	}
	AddressSize GetMemoryAddressSize() override {
		return AddressSize::u8;
	}


public:
	_24AA02E48() {}

	_24AA02E48(I2C_HandleTypeDef &i2c, bool a0 = false, bool a1 = false, bool a2 = false) {
		SetI2c(i2c);
		SetAddress(0b10100000 | ((uint8)a0 << 1) | ((uint8)a1 << 2) | ((uint8)a2 << 3));
	}
};
