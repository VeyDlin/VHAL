#pragma once

#include <BSP.h>

#include "BLEFixedLengthCharacteristic.h"

class BLEConstantCharacteristic: public BLEFixedLengthCharacteristic {
public:
	BLEConstantCharacteristic(const char *uuid, const unsigned char value[], unsigned char length) : BLEFixedLengthCharacteristic(uuid, BLERead, (unsigned char) 0) {
		this->_valueLength = this->_valueSize = length;
		this->_value = (unsigned char*) value;
	}

	BLEConstantCharacteristic(const char *uuid, const char *value) : BLEFixedLengthCharacteristic(uuid, BLERead, (unsigned char) 0) {
		this->_valueLength = this->_valueSize = strlen(value);
		this->_value = (unsigned char*) value;
	}

	virtual ~BLEConstantCharacteristic() {
		this->_value = NULL;// null so super destructor doesn't try to free
	}

	virtual bool setValue(const unsigned char /*value*/[], unsigned char /*length*/) {
		return false;
	}

	virtual bool setValue(const char* /*value*/) {
		return false;
	}
};


