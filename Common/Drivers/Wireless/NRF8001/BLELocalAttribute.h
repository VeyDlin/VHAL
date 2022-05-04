#pragma once
#include "BLEAttribute.h"

class BLELocalAttribute: public BLEAttribute {
	friend class BLEPeripheral;

public:
	BLELocalAttribute(const char *uuid, enum BLEAttributeType type) : BLEAttribute(uuid, type) {
		_numAttributes(true);
	}


protected:
	static unsigned char numAttributes() {
		return _numAttributes();
	}

private:
	static unsigned char _numAttributes(bool isPlus = false) {
		static unsigned char num = 0;
		if(isPlus) {
			num++;
		}
		return num;
	}
};

