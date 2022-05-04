#pragma once
#include <BSP.h>

#include "BLECharacteristic.h"
#include "BLEHIDReportReferenceDescriptor.h"
#include "BLEHID.h"

// From: https://github.com/adafruit/Adafruit-Trinket-USB/blob/master/TrinketHidCombo/TrinketHidCombo.h
//       permission to use under MIT license by @ladyada (https://github.com/adafruit/Adafruit-Trinket-USB/issues/10)

// multimedia keys
#define MMKEY_KB_VOL_UP     0x80 // do not use
#define MMKEY_KB_VOL_DOWN   0x81 // do not use
#define MMKEY_VOL_UP      0xE9
#define MMKEY_VOL_DOWN      0xEA
#define MMKEY_SCAN_NEXT_TRACK 0xB5
#define MMKEY_SCAN_PREV_TRACK 0xB6
#define MMKEY_STOP        0xB7
#define MMKEY_PLAYPAUSE     0xCD
#define MMKEY_MUTE        0xE2
#define MMKEY_BASSBOOST     0xE5
#define MMKEY_LOUDNESS      0xE7
#define MMKEY_KB_EXECUTE    0x74
#define MMKEY_KB_HELP     0x75
#define MMKEY_KB_MENU     0x76
#define MMKEY_KB_SELECT     0x77
#define MMKEY_KB_STOP     0x78
#define MMKEY_KB_AGAIN      0x79
#define MMKEY_KB_UNDO     0x7A
#define MMKEY_KB_CUT      0x7B
#define MMKEY_KB_COPY     0x7C
#define MMKEY_KB_PASTE      0x7D
#define MMKEY_KB_FIND     0x7E
#define MMKEY_KB_MUTE     0x7F // do not use

class BLEMultimedia: public BLEHID {
private:
	static constexpr unsigned char descriptorValue[] = {
	// From: https://github.com/adafruit/Adafruit-Trinket-USB/blob/master/TrinketHidCombo/TrinketHidComboC.c
	//       permission to use under MIT license by @ladyada (https://github.com/adafruit/Adafruit-Trinket-USB/issues/10)

	// this second multimedia key report is what handles the multimedia keys
			0x05, 0x0C,           // USAGE_PAGE (Consumer Devices)
			0x09, 0x01,           // USAGE (Consumer Control)
			0xA1, 0x01,           // COLLECTION (Application)
			0x85, 0x00,           //   REPORT_ID
			0x19, 0x00,           //   USAGE_MINIMUM (Unassigned)
			0x2A, 0x3C, 0x02,     //   USAGE_MAXIMUM
			0x15, 0x00,           //   LOGICAL_MINIMUM (0)
			0x26, 0x3C, 0x02,     //   LOGICAL_MAXIMUM
			0x95, 0x01,           //   REPORT_COUNT (1)
			0x75, 0x10,           //   REPORT_SIZE (16)
			0x81, 0x00,           //   INPUT (Data,Ary,Abs)
			0xC0                  // END_COLLECTION
	};


public:
	BLEMultimedia() :
	BLEHID(descriptorValue, sizeof(descriptorValue), 7),
	_reportCharacteristic("2a4d", BLERead | BLENotify, 2),
	_reportReferenceDescriptor(BLEHIDDescriptorTypeInput)
	{ }

	size_t write(uint8_t k) {
		uint8_t multimediaKeyPress[2] = { 0x00, 0x00 };

		// send key code
		multimediaKeyPress[0] = k;

		for (int i = 0; i < 2; i++) {
			this->sendData(this->_reportCharacteristic, multimediaKeyPress, sizeof(multimediaKeyPress));

			// send cleared code
			multimediaKeyPress[0] = 0x00;
		}

		return 1;
	}

protected:
	virtual void setReportId(unsigned char reportId) {
		BLEHID::setReportId(reportId);
		this->_reportReferenceDescriptor.setReportId(reportId);
	}

	virtual unsigned char numAttributes() {
		return 2;
	}

	virtual BLELocalAttribute** attributes() {
		static BLELocalAttribute *attributes[2];
		attributes[0] = &this->_reportCharacteristic;
		attributes[1] = &this->_reportReferenceDescriptor;
		return attributes;
	}

private:
	BLECharacteristic _reportCharacteristic;
	BLEHIDReportReferenceDescriptor _reportReferenceDescriptor;
};

