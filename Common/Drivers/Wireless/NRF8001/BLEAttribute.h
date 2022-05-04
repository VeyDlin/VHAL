#pragma once

enum BLEAttributeType {
	BLETypeService = 0x2800,
	BLETypeCharacteristic = 0x2803,
	BLETypeDescriptor = 0x2900
};

enum BLEProperty {
	BLEBroadcast = 0x01,
	BLERead = 0x02,
	BLEWriteWithoutResponse = 0x04,
	BLEWrite = 0x08,
	BLENotify = 0x10,
	BLEIndicate = 0x20
};

class BLEAttribute {
public:
	BLEAttribute(const char *uuid, enum BLEAttributeType type) : _uuid(uuid), _type(type) {
	}

	const char* uuid() const {
		return this->_uuid;
	}

	enum BLEAttributeType type() const {
		return this->_type;
	}

private:
	const char *_uuid;
	enum BLEAttributeType _type;
};
