#pragma once
#include "BLEHID.h"
#include "BLEHIDReportMapCharacteristic.h"
#include "BLEPeripheral.h"

class BLEHIDPeripheral: public BLEPeripheral {
	friend class BLEHID;

private:
	static constexpr unsigned char hidInformationCharacteriticValue[] = { 0x11, 0x01, 0x00, 0x03 };

public:
	BLEHIDPeripheral(ASPI *spi, AGPIO *req, AGPIO *rdy, AGPIO *rst) :
	BLEPeripheral(spi, req, rdy, rst),
	_bleBondStore(),
	_hidService("1812"),
	_hidReportMapCharacteristic(),
	_hidInformationCharacteristic("2a4a", hidInformationCharacteriticValue, sizeof(hidInformationCharacteriticValue)),
	_hidControlPointCharacteristic("2a4c", BLEWriteWithoutResponse),
	_reportIdOffset(0),
	_hids(NULL), _numHids(0)
	{
		_instance(this);
	}

	~BLEHIDPeripheral() {
		if (this->_hids) {
			free(this->_hids);
		}
	}

	void begin() {
		this->setBondStore(this->_bleBondStore);

		this->setAdvertisedServiceUuid(this->_hidService.uuid());

		this->addAttribute(this->_hidService);
		this->addAttribute(this->_hidInformationCharacteristic);
		this->addAttribute(this->_hidControlPointCharacteristic);
		this->addAttribute(this->_hidReportMapCharacteristic);

		for (int i = 0; i < this->_numHids; i++) {
			BLEHID *hid = this->_hids[i];

			unsigned char numAttributes = hid->numAttributes();
			BLELocalAttribute **attributes = hid->attributes();

			for (int j = 0; j < numAttributes; j++) {
				this->addAttribute(*attributes[j]);
			}
		}

		this->_hidReportMapCharacteristic.setHids(this->_hids, this->_numHids);

		// begin initialization
		BLEPeripheral::begin();
	}

	void clearBondStoreData() {
		this->_bleBondStore.clearData();
	}

	void setReportIdOffset(unsigned char reportIdOffset) {
		this->_reportIdOffset = reportIdOffset;
	}

	void poll() {
		BLEPeripheral::poll();
	}

	void addHID(BLEHID &hid) {
		if (this->_hids == NULL) {
			this->_hids = (BLEHID**) malloc(sizeof(BLEHID*) * BLEHID::numHids());
		}

		hid.setReportId(this->_numHids + this->_reportIdOffset);

		this->_hids[this->_numHids] = &hid;
		this->_numHids++;
	}

protected:
	static BLEHIDPeripheral* instance() {
		return _instance();
	}

private:
	static BLEHIDPeripheral* _instance(BLEHIDPeripheral *val = nullptr) {
		static BLEHIDPeripheral *_instance;
		if(val != nullptr) {
			_instance = val;
		}
		return _instance;
	}


	BLEBondStore _bleBondStore;

	BLEService _hidService;
	BLEHIDReportMapCharacteristic _hidReportMapCharacteristic;
	BLEProgmemConstantCharacteristic _hidInformationCharacteristic;
	BLEUnsignedCharCharacteristic _hidControlPointCharacteristic;

	unsigned char _reportIdOffset;

	BLEHID **_hids;
	unsigned char _numHids;
};

