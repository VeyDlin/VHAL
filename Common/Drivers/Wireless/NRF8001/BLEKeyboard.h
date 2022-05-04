#pragma once
#include <BSP.h>

#include "BLECharacteristic.h"
#include "BLEHIDReportReferenceDescriptor.h"
#include "BLEHID.h"

// From: https://github.com/adafruit/Adafruit-Trinket-USB/blob/master/TrinketHidCombo/TrinketHidCombo.h
//       permission to use under MIT license by @ladyada (https://github.com/adafruit/Adafruit-Trinket-USB/issues/10)

// LED state masks
#define KB_LED_NUM      0x01
#define KB_LED_CAPS     0x02
#define KB_LED_SCROLL   0x04

// some convenience definitions for modifier keys
#define KEYCODE_MOD_LEFT_CONTROL  0x01
#define KEYCODE_MOD_LEFT_SHIFT    0x02
#define KEYCODE_MOD_LEFT_ALT    0x04
#define KEYCODE_MOD_LEFT_GUI    0x08
#define KEYCODE_MOD_RIGHT_CONTROL 0x10
#define KEYCODE_MOD_RIGHT_SHIFT   0x20
#define KEYCODE_MOD_RIGHT_ALT   0x40
#define KEYCODE_MOD_RIGHT_GUI   0x80

// some more keycodes
#define KEYCODE_LEFT_CONTROL  0xE0
#define KEYCODE_LEFT_SHIFT    0xE1
#define KEYCODE_LEFT_ALT    0xE2
#define KEYCODE_LEFT_GUI    0xE3
#define KEYCODE_RIGHT_CONTROL 0xE4
#define KEYCODE_RIGHT_SHIFT   0xE5
#define KEYCODE_RIGHT_ALT   0xE6
#define KEYCODE_RIGHT_GUI   0xE7
#define KEYCODE_1       0x1E
#define KEYCODE_2       0x1F
#define KEYCODE_3       0x20
#define KEYCODE_4       0x21
#define KEYCODE_5       0x22
#define KEYCODE_6       0x23
#define KEYCODE_7       0x24
#define KEYCODE_8       0x25
#define KEYCODE_9       0x26
#define KEYCODE_0       0x27
#define KEYCODE_A       0x04
#define KEYCODE_B       0x05
#define KEYCODE_C       0x06
#define KEYCODE_D       0x07
#define KEYCODE_E       0x08
#define KEYCODE_F       0x09
#define KEYCODE_G       0x0A
#define KEYCODE_H       0x0B
#define KEYCODE_I       0x0C
#define KEYCODE_J       0x0D
#define KEYCODE_K       0x0E
#define KEYCODE_L       0x0F
#define KEYCODE_M       0x10
#define KEYCODE_N       0x11
#define KEYCODE_O       0x12
#define KEYCODE_P       0x13
#define KEYCODE_Q       0x14
#define KEYCODE_R       0x15
#define KEYCODE_S       0x16
#define KEYCODE_T       0x17
#define KEYCODE_U       0x18
#define KEYCODE_V       0x19
#define KEYCODE_W       0x1A
#define KEYCODE_X       0x1B
#define KEYCODE_Y       0x1C
#define KEYCODE_Z       0x1D
#define KEYCODE_COMMA     0x36
#define KEYCODE_PERIOD      0x37
#define KEYCODE_MINUS     0x2D
#define KEYCODE_EQUAL     0x2E
#define KEYCODE_BACKSLASH   0x31
#define KEYCODE_SQBRAK_LEFT   0x2F
#define KEYCODE_SQBRAK_RIGHT  0x30
#define KEYCODE_SLASH     0x38
#define KEYCODE_F1        0x3A
#define KEYCODE_F2        0x3B
#define KEYCODE_F3        0x3C
#define KEYCODE_F4        0x3D
#define KEYCODE_F5        0x3E
#define KEYCODE_F6        0x3F
#define KEYCODE_F7        0x40
#define KEYCODE_F8        0x41
#define KEYCODE_F9        0x42
#define KEYCODE_F10       0x43
#define KEYCODE_F11       0x44
#define KEYCODE_F12       0x45
#define KEYCODE_APP       0x65
#define KEYCODE_ENTER     0x28
#define KEYCODE_BACKSPACE   0x2A
#define KEYCODE_ESC       0x29
#define KEYCODE_TAB       0x2B
#define KEYCODE_SPACE     0x2C
#define KEYCODE_INSERT      0x49
#define KEYCODE_HOME      0x4A
#define KEYCODE_PAGE_UP     0x4B
#define KEYCODE_DELETE      0x4C
#define KEYCODE_END       0x4D
#define KEYCODE_PAGE_DOWN   0x4E
#define KEYCODE_PRINTSCREEN   0x46
#define KEYCODE_ARROW_RIGHT   0x4F
#define KEYCODE_ARROW_LEFT    0x50
#define KEYCODE_ARROW_DOWN    0x51
#define KEYCODE_ARROW_UP    0x52

#define SHIFT 0x80

// TODO: , public Print
class BLEKeyboard: public BLEHID {
private:
	static constexpr unsigned char descriptorValue[] = {
	  // From: https://github.com/adafruit/Adafruit-Trinket-USB/blob/master/TrinketHidCombo/TrinketHidComboC.c
	  //       permission to use under MIT license by @ladyada (https://github.com/adafruit/Adafruit-Trinket-USB/issues/10)

	  0x05, 0x01,           // USAGE_PAGE (Generic Desktop)
	  0x09, 0x06,           // USAGE (Keyboard)
	  0xA1, 0x01,           // COLLECTION (Application)
	  0x85, 0x00,           // REPORT_ID
	  0x75, 0x01,           //   REPORT_SIZE (1)
	  0x95, 0x08,           //   REPORT_COUNT (8)
	  0x05, 0x07,           //   USAGE_PAGE (Keyboard)(Key Codes)
	  0x19, 0xE0,           //   USAGE_MINIMUM (Keyboard LeftControl)(224)
	  0x29, 0xE7,           //   USAGE_MAXIMUM (Keyboard Right GUI)(231)
	  0x15, 0x00,           //   LOGICAL_MINIMUM (0)
	  0x25, 0x01,           //   LOGICAL_MAXIMUM (1)
	  0x81, 0x02,           //   INPUT (Data,Var,Abs) ; Modifier byte
	  0x95, 0x01,           //   REPORT_COUNT (1)
	  0x75, 0x08,           //   REPORT_SIZE (8)
	  0x81, 0x03,           //   INPUT (Cnst,Var,Abs) ; Reserved byte
	#ifdef USE_LED_REPORT
	  0x95, 0x05,           //   REPORT_COUNT (5)
	  0x75, 0x01,           //   REPORT_SIZE (1)
	  0x05, 0x08,           //   USAGE_PAGE (LEDs)
	  0x19, 0x01,           //   USAGE_MINIMUM (Num Lock)
	  0x29, 0x05,           //   USAGE_MAXIMUM (Kana)
	  0x91, 0x02,           //   OUTPUT (Data,Var,Abs) ; LED report
	  0x95, 0x01,           //   REPORT_COUNT (1)
	  0x75, 0x03,           //   REPORT_SIZE (3)
	  0x91, 0x03,           //   OUTPUT (Cnst,Var,Abs) ; LED report padding
	#endif
	  0x95, 0x05,           //   REPORT_COUNT (5)
	  0x75, 0x08,           //   REPORT_SIZE (8)
	  0x15, 0x00,           //   LOGICAL_MINIMUM (0)
	  0x26, 0xA4, 0x00,     //   LOGICAL_MAXIMUM (164)
	  0x05, 0x07,           //   USAGE_PAGE (Keyboard)(Key Codes)
	  0x19, 0x00,           //   USAGE_MINIMUM (Reserved (no event indicated))(0)
	  0x2A, 0xA4, 0x00,     //   USAGE_MAXIMUM (Keyboard Application)(164)
	  0x81, 0x00,           //   INPUT (Data,Ary,Abs)
	  0xC0                  // END_COLLECTION
	};


	static constexpr unsigned char asciiMap[] = {
	  0x0,
	  0x0,
	  0x0,
	  0x0,
	  0x0,
	  0x0,
	  0x0,
	  0x0,
	  KEYCODE_BACKSPACE,          // '\b'
	  KEYCODE_TAB,                // '\t'
	  KEYCODE_ENTER,              // '\n'
	  0x0,
	  0x0,
	  0x0,
	  0x0,
	  0x0,
	  0x0,
	  0x0,
	  0x0,
	  0x0,
	  0x0,
	  0x0,
	  0x0,
	  0x0,
	  0x0,
	  0x0,
	  0x0,
	  0x0,
	  0x0,
	  0x0,
	  0x0,
	  0x0,
	  KEYCODE_SPACE,              // ' '
	  0x1e | SHIFT,               // '!'
	  0x34 | SHIFT,               // '"'
	  0x20 | SHIFT,               // '#'
	  0x21 | SHIFT,               // '$'
	  0x22 | SHIFT,               // '%'
	  0x24 | SHIFT,               // '&'
	  0x34,                       // '''
	  0x26 | SHIFT,               // '('
	  0x27 | SHIFT,               // ')'
	  0x25 | SHIFT,               // '*'
	  0x2e | SHIFT,               // '+'
	  KEYCODE_COMMA,              // ','
	  KEYCODE_MINUS,              // '-'
	  KEYCODE_PERIOD,             // '.'
	  KEYCODE_SLASH,              // '/'
	  KEYCODE_0,                  // '0'
	  KEYCODE_1,                  // '1'
	  KEYCODE_2,                  // '2'
	  KEYCODE_3,                  // '3'
	  KEYCODE_4,                  // '4'
	  KEYCODE_5,                  // '5'
	  KEYCODE_6,                  // '6'
	  KEYCODE_7,                  // '7'
	  KEYCODE_8,                  // '8'
	  KEYCODE_9,                  // '9'
	  0x33 | SHIFT,               // ':'
	  0x33,                       // ';'
	  0x36 | SHIFT,               // '<'
	  KEYCODE_EQUAL,              // '='
	  0x37 | SHIFT,               // '>'
	  0x38 | SHIFT,               // '?'
	  0x1f | SHIFT,               // '@'
	  KEYCODE_A | SHIFT,          // 'A'
	  KEYCODE_B | SHIFT,          // 'B'
	  KEYCODE_C | SHIFT,          // 'C'
	  KEYCODE_D | SHIFT,          // 'D'
	  KEYCODE_E | SHIFT,          // 'E'
	  KEYCODE_F | SHIFT,          // 'F'
	  KEYCODE_G | SHIFT,          // 'G'
	  KEYCODE_H | SHIFT,          // 'H'
	  KEYCODE_I | SHIFT,          // 'I'
	  KEYCODE_J | SHIFT,          // 'J'
	  KEYCODE_K | SHIFT,          // 'K'
	  KEYCODE_L | SHIFT,          // 'L'
	  KEYCODE_M | SHIFT,          // 'M'
	  KEYCODE_N | SHIFT,          // 'N'
	  KEYCODE_O | SHIFT,          // 'O'
	  KEYCODE_P | SHIFT,          // 'P'
	  KEYCODE_Q | SHIFT,          // 'Q'
	  KEYCODE_R | SHIFT,          // 'R'
	  KEYCODE_S | SHIFT,          // 'S'
	  KEYCODE_T | SHIFT,          // 'T'
	  KEYCODE_U | SHIFT,          // 'U'
	  KEYCODE_V | SHIFT,          // 'V'
	  KEYCODE_W | SHIFT,          // 'W'
	  KEYCODE_X | SHIFT,          // 'X'
	  KEYCODE_Y | SHIFT,          // 'Y'
	  KEYCODE_Z | SHIFT,          // 'Z'
	  KEYCODE_SQBRAK_LEFT,        // '['
	  KEYCODE_BACKSLASH,          // '\'
	  KEYCODE_SQBRAK_RIGHT,       // ']'
	  0x23 | SHIFT,               // '^'
	  0x2d | SHIFT,               // '_'
	  0x35,                       // '`'
	  KEYCODE_A,                  // 'a'
	  KEYCODE_B,                  // 'b'
	  KEYCODE_C,                  // 'c'
	  KEYCODE_D,                  // 'd'
	  KEYCODE_E,                  // 'e'
	  KEYCODE_F,                  // 'f'
	  KEYCODE_G,                  // 'g'
	  KEYCODE_H,                  // 'h'
	  KEYCODE_I,                  // 'i'
	  KEYCODE_J,                  // 'j'
	  KEYCODE_K,                  // 'k'
	  KEYCODE_L,                  // 'l'
	  KEYCODE_M,                  // 'm'
	  KEYCODE_N,                  // 'n'
	  KEYCODE_O,                  // 'o'
	  KEYCODE_P,                  // 'p'
	  KEYCODE_Q,                  // 'q'
	  KEYCODE_R,                  // 'r'
	  KEYCODE_S,                  // 's'
	  KEYCODE_T,                  // 't'
	  KEYCODE_U,                  // 'u'
	  KEYCODE_V,                  // 'v'
	  KEYCODE_W,                  // 'w'
	  KEYCODE_X,                  // 'x'
	  KEYCODE_Y,                  // 'y'
	  KEYCODE_Z,                  // 'z'
	  0x2f | SHIFT,               // '{'
	  0x31 | SHIFT,               // '|'
	  0x30 | SHIFT,               // '}'
	  0x35 | SHIFT,               // '~'
	  0x0
	};

public:
	BLEKeyboard() :
	BLEHID(descriptorValue, sizeof(descriptorValue), 7),
	_reportCharacteristic("2a4d", BLERead | BLENotify, 8),
	_reportReferenceDescriptor(BLEHIDDescriptorTypeInput)
	{
		memset(this->_value, 0, sizeof(this->_value));
	}

	size_t write(uint8_t k) {
		uint8_t code;
		uint8_t modifier;

		this->keyToCodeAndModifier(k, code, modifier);

		return (this->press(code, modifier) && this->release(code, modifier));
	}

	size_t press(uint8_t code, uint8_t modifiers) {
		size_t written = 0;

		if (code != 0) {
			for (unsigned int i = 2; i < sizeof(this->_value); i++) {
				if (this->_value[i] == 0) {
					this->_value[0] |= modifiers;
					this->_value[i] = code;

					written = 1;
					break;
				}
			}
		} else if (modifiers) {
			this->_value[0] |= modifiers;
			written = 1;
		}

		if (written) {
			this->sendValue();
		}

		return written;
	}

	size_t release(uint8_t code, uint8_t modifiers) {
		size_t cleared = 0;

		if (code != 0) {
			for (unsigned int i = 2; i < sizeof(this->_value); i++) {
				if (this->_value[i] == code) {
					this->_value[0] &= ~modifiers;
					this->_value[i] = 0;

					cleared = 1;
					break;
				}
			}
		} else if (modifiers) {
			this->_value[0] &= ~modifiers;
			cleared = 1;
		}

		if (cleared) {
			this->sendValue();
		}

		return cleared;
	}

	void releaseAll(void) {
		memset(this->_value, 0, sizeof(this->_value));

		this->sendValue();
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
	void sendValue() {
		BLEHID::sendData(this->_reportCharacteristic, this->_value, sizeof(this->_value));
	}

	void keyToCodeAndModifier(uint8_t k, uint8_t &code, uint8_t &modifier) {
		code = 0;
		modifier = 0;

		if (k < 128) {
			code = asciiMap[k];

			if (code & 0x80) {
				modifier = KEYCODE_MOD_LEFT_SHIFT;
				code &= 0x7f;
			}
		}
	}

private:
	BLECharacteristic _reportCharacteristic;
	BLEHIDReportReferenceDescriptor _reportReferenceDescriptor;

	unsigned char _value[8];
};

