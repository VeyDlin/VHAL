#pragma once

#include "BLERemoteAttribute.h"
#include "BLEDeviceLimits.h"

enum BLERemoteCharacteristicEvent {
	BLEValueUpdated = 0
};

class BLECentral;
class BLERemoteCharacteristic;

class BLERemoteCharacteristicValueChangeListener {
public:
	virtual bool canReadRemoteCharacteristic(BLERemoteCharacteristic& /*characteristic*/) {
		return false;
	}
	virtual bool readRemoteCharacteristic(BLERemoteCharacteristic& /*characteristic*/) {
		return false;
	}

	virtual bool canWriteRemoteCharacteristic(BLERemoteCharacteristic& /*characteristic*/) {
		return false;
	}
	virtual bool writeRemoteCharacteristic(BLERemoteCharacteristic& /*characteristic*/, const unsigned char /*value*/[], unsigned char /*length*/) {
		return false;
	}

	virtual bool canSubscribeRemoteCharacteristic(BLERemoteCharacteristic& /*characteristic*/) {
		return false;
	}
	virtual bool subscribeRemoteCharacteristic(BLERemoteCharacteristic& /*characteristic*/) {
		return false;
	}
	virtual bool canUnsubscribeRemoteCharacteristic(BLERemoteCharacteristic& /*characteristic*/) {
		return false;
	}
	virtual bool unsubcribeRemoteCharacteristic(BLERemoteCharacteristic& /*characteristic*/) {
		return false;
	}
};

typedef void (*BLERemoteCharacteristicEventHandler)(BLECentral &central, BLERemoteCharacteristic &characteristic);

class BLERemoteCharacteristic: public BLERemoteAttribute {
	friend class BLEPeripheral;

public:
	BLERemoteCharacteristic(const char *uuid, unsigned char properties) :
		BLERemoteAttribute(uuid, BLETypeCharacteristic), _properties(properties), _valueLength(0), _valueUpdated(false), _listener(NULL)
	{
		memset(this->_eventHandlers, 0x00, sizeof(this->_eventHandlers));
	}

	virtual ~BLERemoteCharacteristic() {
	}

	unsigned char properties() const {
		return this->_properties;
	}

	const unsigned char* value() const {
		return this->_value;
	}

	unsigned char valueLength() const {
		return this->_valueLength;
	}

	bool canRead() {
		bool result = false;

		if (this->_listener) {
			result = this->_listener->canReadRemoteCharacteristic(*this);
		}

		return result;
	}

	bool read() {
		bool result = false;

		if (this->_listener) {
			result = this->_listener->readRemoteCharacteristic(*this);
		}

		return result;
	}

	bool canWrite() {
		bool result = false;

		if (this->_listener) {
			result = this->_listener->canWriteRemoteCharacteristic(*this);
		}

		return result;
	}

	bool write(const unsigned char value[], unsigned char length) {
		bool result = false;

		if (this->_listener) {
			result = this->_listener->writeRemoteCharacteristic(*this, value, length);
		}

		return result;
	}

	bool canSubscribe() {
		bool result = false;

		if (this->_listener) {
			result = this->_listener->canSubscribeRemoteCharacteristic(*this);
		}

		return result;
	}

	bool subscribe() {
		bool result = false;

		if (this->_listener) {
			result = this->_listener->subscribeRemoteCharacteristic(*this);
		}

		return result;
	}

	bool canUnsubscribe() {
		bool result = false;

		if (this->_listener) {
			result = this->_listener->canUnsubscribeRemoteCharacteristic(*this);
		}

		return result;
	}

	bool unsubscribe() {
		bool result = false;

		if (this->_listener) {
			result = this->_listener->unsubcribeRemoteCharacteristic(*this);
		}

		return result;
	}

	bool valueUpdated() {
		bool valueUpdated = this->_valueUpdated;

		this->_valueUpdated = false;

		return valueUpdated;
	}

	void setEventHandler(BLERemoteCharacteristicEvent event, BLERemoteCharacteristicEventHandler eventHandler) {
		if (event < sizeof(this->_eventHandlers)) {
			this->_eventHandlers[event] = eventHandler;
		}
	}


protected:
	void setValue(BLECentral &central, const unsigned char value[], unsigned char length) {
		this->_valueLength = length;
		memcpy(this->_value, value, length);

		this->_valueUpdated = true;

		BLERemoteCharacteristicEventHandler eventHandler = this->_eventHandlers[BLEValueUpdated];
		if (eventHandler) {
			eventHandler(central, *this);
		}
	}

	void setValueChangeListener(BLERemoteCharacteristicValueChangeListener &listener) {
		this->_listener = &listener;
	}


private:
	unsigned char _properties;

	unsigned char _valueLength;
	unsigned char _value[BLE_REMOTE_ATTRIBUTE_MAX_VALUE_LENGTH];

	bool _valueUpdated;

	BLERemoteCharacteristicValueChangeListener *_listener;
	BLERemoteCharacteristicEventHandler _eventHandlers[1];
};

