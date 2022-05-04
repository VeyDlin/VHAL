#pragma once
#include "BLECharacteristic.h"

class BLEFixedLengthCharacteristic: public BLECharacteristic {
public:
	BLEFixedLengthCharacteristic(const char *uuid, unsigned char properties, unsigned char valueSize) : BLECharacteristic(uuid, properties, valueSize) {
		this->_valueLength = valueSize;
	}

	BLEFixedLengthCharacteristic(const char *uuid, unsigned char properties, const char *value) : BLECharacteristic(uuid, properties, value) {
	}

	bool fixedLength() const {
		return true;
	}
};

