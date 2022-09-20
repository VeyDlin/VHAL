#pragma once
#include "BLETypedCharacteristic.h"

class BLEBoolCharacteristic: public BLETypedCharacteristic<bool> {
public:
	BLEBoolCharacteristic(const char *uuid, unsigned char properties) : BLETypedCharacteristic<bool>(uuid, properties) {
	}
};

class BLECharCharacteristic: public BLETypedCharacteristic<char> {
public:
	BLECharCharacteristic(const char *uuid, unsigned char properties) : BLETypedCharacteristic<char>(uuid, properties) {
	}
};

class BLEUnsignedCharCharacteristic: public BLETypedCharacteristic<unsigned char> {
public:
	BLEUnsignedCharCharacteristic(const char *uuid, unsigned char properties) : BLETypedCharacteristic<unsigned char>(uuid, properties) {
	}
};

class BLEShortCharacteristic: public BLETypedCharacteristic<short> {
public:
	BLEShortCharacteristic(const char *uuid, unsigned char properties) : BLETypedCharacteristic<short>(uuid, properties) {
	}
};

class BLEUnsignedShortCharacteristic: public BLETypedCharacteristic<unsigned short> {
public:
	BLEUnsignedShortCharacteristic(const char *uuid, unsigned char properties): BLETypedCharacteristic<unsigned short>(uuid, properties) {
	}
};

class BLEIntCharacteristic: public BLETypedCharacteristic<int> {
public:
	BLEIntCharacteristic(const char *uuid, unsigned char properties) : BLETypedCharacteristic<int>(uuid, properties) {
	}
};

class BLEUnsignedIntCharacteristic: public BLETypedCharacteristic<unsigned int> {
public:
	BLEUnsignedIntCharacteristic(const char *uuid, unsigned char properties) : BLETypedCharacteristic<unsigned int>(uuid, properties) {
	}
};

class BLELongCharacteristic: public BLETypedCharacteristic<long> {
public:
	BLELongCharacteristic(const char *uuid, unsigned char properties) : BLETypedCharacteristic<long>(uuid, properties) {
	}
};

class BLEUnsignedLongCharacteristic: public BLETypedCharacteristic<unsigned long> {
public:
	BLEUnsignedLongCharacteristic(const char *uuid, unsigned char properties) : BLETypedCharacteristic<unsigned long>(uuid, properties) {
	}
};

class BLEFloatCharacteristic: public BLETypedCharacteristic<float> {
public:
	BLEFloatCharacteristic(const char *uuid, unsigned char properties) : BLETypedCharacteristic<float>(uuid, properties) {
	}

};

class BLEDoubleCharacteristic: public BLETypedCharacteristic<double> {
public:
	BLEDoubleCharacteristic(const char *uuid, unsigned char properties) : BLETypedCharacteristic<double>(uuid, properties) {
	}
};

