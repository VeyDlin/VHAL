#pragma once
#include "BLEDescriptor.h"

enum BLEHIDDescriptorType {
	BLEHIDDescriptorTypeInput = 0x01,
	BLEHIDDescriptorTypeOutput = 0x02
};

class BLEHIDReportReferenceDescriptor: public BLEDescriptor {
public:
	BLEHIDReportReferenceDescriptor(BLEHIDDescriptorType type) : BLEDescriptor("2908", this->_value, sizeof(_value)) {
		this->_value[0] = 0x00;
		this->_value[1] = type;
	}

	virtual ~BLEHIDReportReferenceDescriptor() {
	}

	void setReportId(unsigned char reportId) {
		this->_value[0] = reportId;
	}

private:
	unsigned char _value[2];
};

