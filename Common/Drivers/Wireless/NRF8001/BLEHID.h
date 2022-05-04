#pragma once
#include "BLELocalAttribute.h"
#include "BLECharacteristic.h"


class BLEHID {
	friend class BLEHIDPeripheral;

public:
	BLEHID(const unsigned char *descriptor, unsigned char descriptorLength, unsigned char reportIdOffset) : _reportId(0), _descriptor(descriptor), _descriptorLength(descriptorLength), _reportIdOffset(reportIdOffset) {
		_numHids++;
	}

	unsigned char getDescriptorLength() {
		return this->_descriptorLength;
	}

	unsigned char getDescriptorValueAtOffset(unsigned char offset) {
		if (offset == this->_reportIdOffset && this->_reportIdOffset) {
			return this->_reportId;
		} else {
			return this->_descriptor[offset];
		}
	}


protected:
	static unsigned char numHids() {
		return _numHids;
	}

	void sendData(BLECharacteristic &characteristic, unsigned char data[], unsigned char length);

	virtual void setReportId(unsigned char reportId) {
		this->_reportId = reportId;
	}

	virtual unsigned char numAttributes() {
		return 0;
	}
	virtual BLELocalAttribute** attributes() {
		return 0;
	}


private:
	static unsigned char _numHids;

	unsigned char _reportId;
	const unsigned char *_descriptor;
	unsigned char _descriptorLength;
	unsigned char _reportIdOffset;
};

