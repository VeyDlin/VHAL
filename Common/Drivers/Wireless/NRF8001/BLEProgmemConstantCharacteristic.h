#pragma once
#include <BSP.h>
#include "BLEConstantCharacteristic.h"

class BLEProgmemConstantCharacteristic: public BLEConstantCharacteristic {
public:
	BLEProgmemConstantCharacteristic(const char *uuid, const unsigned char value[], unsigned char length) : BLEConstantCharacteristic(uuid, value, length) {
	}

	BLEProgmemConstantCharacteristic(const char *uuid, const char *value) : BLEConstantCharacteristic(uuid, value) {
	}

	virtual ~BLEProgmemConstantCharacteristic() {
	}

	virtual unsigned char operator[](int offset) const {
		return this->_value[offset];
	}

};

