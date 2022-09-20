#pragma once

#include <BSP.h>
#include "BLEDeviceLimits.h"
#include "BLEDescriptor.h"
#include "BLELocalAttribute.h"

class BLEDescriptor: public BLELocalAttribute {
public:
	BLEDescriptor(const char *uuid, const unsigned char value[], unsigned char valueLength) : BLELocalAttribute(uuid, BLETypeDescriptor), _value(value), _valueLength(valueLength) {
	}

	BLEDescriptor(const char *uuid, const char *value) : BLELocalAttribute(uuid, BLETypeDescriptor), _value((const unsigned char*) value), _valueLength(strlen(value)) {
	}

	virtual	~BLEDescriptor() {
	}

	virtual const unsigned char* value() const {
		return this->_value;
	}

	virtual unsigned char valueLength() const {
		return this->_valueLength;
	}

	virtual unsigned char operator[](int offset) const {
		return this->_value[offset];
	}



private:
	const char *_uuid;
	const unsigned char *_value;
	unsigned char _valueLength;
};

