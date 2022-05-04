#pragma once
#include "BLEHID.h"
#include "BLEConstantCharacteristic.h"

class BLEHIDReportMapCharacteristic: public BLEConstantCharacteristic {
	friend class BLEHID;

private:
	BLEHID **_hids;
	unsigned char _numHids;

public:
	BLEHIDReportMapCharacteristic() : BLEConstantCharacteristic("2a4b", NULL, 0), _hids(NULL), _numHids(0) {

	}

	virtual unsigned char valueSize() const {
		unsigned char valueSize = 0;

		for (unsigned char i = 0; i < this->_numHids; i++) {
			valueSize += this->_hids[i]->getDescriptorLength();
		}

		return valueSize;
	}

	virtual unsigned char valueLength() const {
		return this->valueSize();
	}

	virtual unsigned char operator[](int offset) const {
		unsigned char value = 0x00;
		unsigned char totalOffset = 0;

		for (unsigned char i = 0; i < this->_numHids; i++) {
			unsigned char descriptorLength = this->_hids[i]->getDescriptorLength();

			if ((offset >= totalOffset) && (offset < (totalOffset + descriptorLength))) {
				value = this->_hids[i]->getDescriptorValueAtOffset(offset - totalOffset);
				break;
			}

			totalOffset += descriptorLength;
		}

		return value;
	}

	void setHids(BLEHID **hids, unsigned char numHids) {
		this->_hids = hids;
		this->_numHids = numHids;
	}
};

