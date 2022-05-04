#pragma once
#include <BSP.h>

#include "BLEDeviceLimits.h"
#include "BLECharacteristic.h"
#include "BLELocalAttribute.h"

enum BLECharacteristicEvent {
	BLEWritten = 0,
	BLESubscribed = 1,
	BLEUnsubscribed = 2
};

class BLECentral;
class BLECharacteristic;

typedef void (*BLECharacteristicEventHandler)(BLECentral &central, BLECharacteristic &characteristic);

class BLECharacteristicValueChangeListener {
public:
	virtual bool characteristicValueChanged(BLECharacteristic& /*characteristic*/) {
		return false;
	}
	virtual bool broadcastCharacteristic(BLECharacteristic& /*characteristic*/) {
		return false;
	}
	virtual bool canNotifyCharacteristic(BLECharacteristic& /*characteristic*/) {
		return false;
	}
	virtual bool canIndicateCharacteristic(BLECharacteristic& /*characteristic*/) {
		return false;
	}
};

class BLECharacteristic: public BLELocalAttribute {
	friend class BLEPeripheral;

public:
	BLECharacteristic(const char *uuid, unsigned char properties, unsigned char valueSize) :
			BLELocalAttribute(uuid, BLETypeCharacteristic), _valueSize(VMath::Min<unsigned char>(valueSize, BLE_ATTRIBUTE_MAX_VALUE_LENGTH)),
			_value(NULL), _valueLength(0), _properties(properties), _written(false), _subscribed(false), _listener(NULL)
		{
		memset(this->_eventHandlers, 0x00, sizeof(this->_eventHandlers));

		if (valueSize) {
			this->_value = (unsigned char*) malloc(this->_valueSize);
		}
	}

	BLECharacteristic(const char *uuid, unsigned char properties, const char *value) :
			BLELocalAttribute(uuid, BLETypeCharacteristic), _valueSize(VMath::Min<unsigned char>(strlen(value), BLE_ATTRIBUTE_MAX_VALUE_LENGTH)),
			_value(NULL), _valueLength(0), _properties(properties), _written(false), _subscribed(false), _listener(NULL)
		{
		memset(this->_eventHandlers, 0x00, sizeof(this->_eventHandlers));

		this->_value = (unsigned char*) malloc(this->_valueSize);
		this->setValue(value);
	}

	virtual ~BLECharacteristic() {
		if (this->_value) {
			free(this->_value);
		}
	}

	unsigned char properties() const {
		return this->_properties;
	}

	virtual unsigned char valueSize() const {
		return this->_valueSize;
	}

	virtual unsigned const char* value() const {
		return this->_value;
	}

	virtual unsigned char valueLength() const {
		return this->_valueLength;
	}

	virtual bool fixedLength() const {
		return false;
	}

	virtual unsigned char operator[](int offset) const {
		return this->_value[offset];
	}



	virtual bool setValue(const char *value) {
		return this->setValue((const unsigned char*) value, strlen(value));
	}

	bool broadcast() {
		bool success = false;

		if (this->_listener) {
			success = this->_listener->broadcastCharacteristic(*this);
		}

		return success;
	}

	bool written() {
		bool written = this->_written;

		this->_written = false;

		return written;
	}

	virtual bool setValue(const unsigned char value[], unsigned char length) {
		bool success = true;

		this->_valueLength = VMath::Min<unsigned char>(length, this->_valueSize);

		memcpy(this->_value, value, this->_valueLength);

		if (this->_listener) {
			success = this->_listener->characteristicValueChanged(*this);
		}

		return success;
	}


	bool subscribed() {
		return this->_subscribed;
	}

	bool canNotify() {
		return (this->_listener) ? this->_listener->canNotifyCharacteristic(*this) : false;
	}

	bool canIndicate() {
		return (this->_listener) ? this->_listener->canIndicateCharacteristic(*this) : false;
	}

	void setEventHandler(BLECharacteristicEvent event, BLECharacteristicEventHandler eventHandler) {
		if (event < sizeof(this->_eventHandlers)) {
			this->_eventHandlers[event] = eventHandler;
		}
	}



protected:
	void setValue(BLECentral &central, const unsigned char value[], unsigned char length) {
		this->setValue(value, length);

		this->_written = true;

		BLECharacteristicEventHandler eventHandler = this->_eventHandlers[BLEWritten];
		if (eventHandler) {
			eventHandler(central, *this);
		}
	}

	void setSubscribed(BLECentral &central, bool subscribed) {
		this->_subscribed = subscribed;

		BLECharacteristicEventHandler eventHandler = this->_eventHandlers[subscribed ? BLESubscribed : BLEUnsubscribed];

		if (eventHandler) {
			eventHandler(central, *this);
		}
	}

	void setValueChangeListener(BLECharacteristicValueChangeListener &listener) {
		this->_listener = &listener;
	}

	unsigned char _valueSize;
	unsigned char *_value;
	unsigned char _valueLength;

private:
	unsigned char _properties;

	bool _written;
	bool _subscribed;

	BLECharacteristicValueChangeListener *_listener;
	BLECharacteristicEventHandler _eventHandlers[3];
};
