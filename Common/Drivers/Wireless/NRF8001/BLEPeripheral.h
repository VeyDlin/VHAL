#pragma once
#include <BSP.h>
#include "BLEBondStore.h"
#include "BLECentral.h"
#include "BLEConstantCharacteristic.h"
#include "BLEDescriptor.h"
#include "BLEDevice.h"
#include "BLEFixedLengthCharacteristic.h"
#include "BLELocalAttribute.h"
#include "BLEProgmemConstantCharacteristic.h"
#include "BLERemoteAttribute.h"
#include "BLERemoteCharacteristic.h"
#include "BLERemoteService.h"
#include "BLEService.h"
#include "BLETypedCharacteristics.h"
#include "nRF8001.h"
#include "BLEUuid.h"
#include "BLEDeviceLimits.h"
#include "BLEUtil.h"


//#define BLE_PERIPHERAL_DEBUG

#define DEFAULT_DEVICE_NAME "NRF8001"
#define DEFAULT_APPEARANCE  0x0000



enum BLEPeripheralEvent {
  BLEConnected = 0,
  BLEDisconnected = 1,
  BLEBonded = 2,
  BLERemoteServicesDiscovered = 3
};

typedef void (*BLEPeripheralEventHandler)(BLECentral& central);


class BLEPeripheral : public BLEDeviceEventListener, public BLECharacteristicValueChangeListener, public BLERemoteCharacteristicValueChangeListener {
private:
  BLEDevice*                     _device;
  nRF8001                        _nRF8001;

  const char*                    _advertisedServiceUuid;
  const char*                    _serviceSolicitationUuid;
  const unsigned char*           _manufacturerData;
  unsigned char                  _manufacturerDataLength;
  const char*                    _localName;

  BLELocalAttribute**            _localAttributes;
  unsigned char                  _numLocalAttributes;
  BLERemoteAttribute**           _remoteAttributes;
  unsigned char                  _numRemoteAttributes;

  BLEService                     _genericAccessService;
  BLECharacteristic              _deviceNameCharacteristic;
  BLECharacteristic              _appearanceCharacteristic;
  BLEService                     _genericAttributeService;
  BLECharacteristic              _servicesChangedCharacteristic;

  BLERemoteService               _remoteGenericAttributeService;
  BLERemoteCharacteristic        _remoteServicesChangedCharacteristic;

  BLECentral                     _central;
  BLEPeripheralEventHandler      _eventHandlers[4];

public:
	BLEPeripheral(ASPI *spi, AGPIO *req, AGPIO *rdy, AGPIO *rst) :
	  _nRF8001(spi, req, rdy, rst),

	  _advertisedServiceUuid(NULL),
	  _serviceSolicitationUuid(NULL),
	  _manufacturerData(NULL),
	  _manufacturerDataLength(0),
	  _localName(NULL),

	  _localAttributes(NULL),
	  _numLocalAttributes(0),
	  _remoteAttributes(NULL),
	  _numRemoteAttributes(0),

	  _genericAccessService("1800"),
	  _deviceNameCharacteristic("2a00", BLERead, 19),
	  _appearanceCharacteristic("2a01", BLERead, 2),
	  _genericAttributeService("1801"),
	  _servicesChangedCharacteristic("2a05", BLEIndicate, 4),

	  _remoteGenericAttributeService("1801"),
	  _remoteServicesChangedCharacteristic("2a05", BLEIndicate),

	  _central(this)
	{
	  this->_device = &this->_nRF8001;

	  memset(this->_eventHandlers, 0x00, sizeof(this->_eventHandlers));

	  this->setDeviceName(DEFAULT_DEVICE_NAME);
	  this->setAppearance(DEFAULT_APPEARANCE);

	  this->_device->setEventListener(this);
	}


    virtual ~BLEPeripheral() {
    	  this->end();

    	  if (this->_remoteAttributes) {
    	    free(this->_remoteAttributes);
    	  }

    	  if (this->_localAttributes) {
    	    free(this->_localAttributes);
    	  }
    	}




    void begin() {
      unsigned char advertisementDataSize = 0;

      BLEEirData advertisementData[3];
      BLEEirData scanData;

      scanData.length = 0;

      unsigned char remainingAdvertisementDataLength = BLE_ADVERTISEMENT_DATA_MAX_VALUE_LENGTH + 2;
      if (this->_serviceSolicitationUuid){
        BLEUuid serviceSolicitationUuid = BLEUuid(this->_serviceSolicitationUuid);

        unsigned char uuidLength = serviceSolicitationUuid.length();
        advertisementData[advertisementDataSize].length = uuidLength;
        advertisementData[advertisementDataSize].type = (uuidLength > 2) ? 0x15 : 0x14;

        memcpy(advertisementData[advertisementDataSize].data, serviceSolicitationUuid.data(), uuidLength);
        advertisementDataSize += 1;
        remainingAdvertisementDataLength -= uuidLength + 2;
      }
      if (this->_advertisedServiceUuid){
        BLEUuid advertisedServiceUuid = BLEUuid(this->_advertisedServiceUuid);

        unsigned char uuidLength = advertisedServiceUuid.length();
        if (uuidLength + 2 <= remainingAdvertisementDataLength) {
          advertisementData[advertisementDataSize].length = uuidLength;
          advertisementData[advertisementDataSize].type = (uuidLength > 2) ? 0x06 : 0x02;

          memcpy(advertisementData[advertisementDataSize].data, advertisedServiceUuid.data(), uuidLength);
          advertisementDataSize += 1;
          remainingAdvertisementDataLength -= uuidLength + 2;
        }
      }
      if (this->_manufacturerData && this->_manufacturerDataLength > 0) {
        if (remainingAdvertisementDataLength >= 3) {
          unsigned char dataLength = this->_manufacturerDataLength;

          if (dataLength + 2 > remainingAdvertisementDataLength) {
            dataLength = remainingAdvertisementDataLength - 2;
          }

          advertisementData[advertisementDataSize].length = dataLength;
          advertisementData[advertisementDataSize].type = 0xff;

          memcpy(advertisementData[advertisementDataSize].data, this->_manufacturerData, dataLength);
          advertisementDataSize += 1;
          remainingAdvertisementDataLength -= dataLength + 2;
        }
      }

      if (this->_localName){
        unsigned char localNameLength = strlen(this->_localName);
        scanData.length = localNameLength;

        if (scanData.length > BLE_SCAN_DATA_MAX_VALUE_LENGTH) {
          scanData.length = BLE_SCAN_DATA_MAX_VALUE_LENGTH;
        }

        scanData.type = (localNameLength > scanData.length) ? 0x08 : 0x09;

        memcpy(scanData.data, this->_localName, scanData.length);
      }

      if (this->_localAttributes == NULL) {
        this->initLocalAttributes();
      }

      for (int i = 0; i < this->_numLocalAttributes; i++) {
        BLELocalAttribute* localAttribute = this->_localAttributes[i];
        if (localAttribute->type() == BLETypeCharacteristic) {
          BLECharacteristic* characteristic = (BLECharacteristic*)localAttribute;

          characteristic->setValueChangeListener(*this);
        }
      }

      for (int i = 0; i < this->_numRemoteAttributes; i++) {
        BLERemoteAttribute* remoteAttribute = this->_remoteAttributes[i];
        if (remoteAttribute->type() == BLETypeCharacteristic) {
          BLERemoteCharacteristic* remoteCharacteristic = (BLERemoteCharacteristic*)remoteAttribute;

          remoteCharacteristic->setValueChangeListener(*this);
        }
      }

      if (this->_numRemoteAttributes) {
        this->addRemoteAttribute(this->_remoteGenericAttributeService);
        this->addRemoteAttribute(this->_remoteServicesChangedCharacteristic);
      }

      this->_device->begin(advertisementDataSize, advertisementData,
                            scanData.length > 0 ? 1 : 0, &scanData,
                            this->_localAttributes, this->_numLocalAttributes,
                            this->_remoteAttributes, this->_numRemoteAttributes);

      this->_device->requestAddress();
    }

    void poll() {
      this->_device->poll();
    }

    void end() {
      this->_device->end();
    }

    void setAdvertisedServiceUuid(const char* advertisedServiceUuid) {
      this->_advertisedServiceUuid = advertisedServiceUuid;
    }

    void setServiceSolicitationUuid(const char* serviceSolicitationUuid) {
      this->_serviceSolicitationUuid = serviceSolicitationUuid;
    }

    void setManufacturerData(const unsigned char manufacturerData[], unsigned char manufacturerDataLength) {
      this->_manufacturerData = manufacturerData;
      this->_manufacturerDataLength = manufacturerDataLength;
    }

    void setLocalName(const char* localName) {
      this->_localName = localName;
    }

    void setAdvertisingInterval(unsigned short advertisingInterval) {
      this->_device->setAdvertisingInterval(advertisingInterval);
    }


    // connection intervals in 1.25 ms increments,
    // must be between  0x0006 (7.5 ms) and 0x0c80 (4 s), values outside of this range will be ignored
    void setConnectionInterval(unsigned short minimumConnectionInterval, unsigned short maximumConnectionInterval) {
      this->_device->setConnectionInterval(minimumConnectionInterval, maximumConnectionInterval);
    }

    bool  setTxPower(int txPower) {
      return this->_device->setTxPower(txPower);
    }

    void setConnectable(bool connectable) {
      this->_device->setConnectable(connectable);
    }

    void setBondStore(BLEBondStore& bondStore) {
      this->_device->setBondStore(bondStore);
    }


    void setDeviceName(const char* deviceName) {
      this->_deviceNameCharacteristic.setValue(deviceName);
    }

    void setAppearance(unsigned short appearance) {
      this->_appearanceCharacteristic.setValue((unsigned char *)&appearance, sizeof(appearance));
    }

    void addAttribute(BLELocalAttribute& attribute) {
      this->addLocalAttribute(attribute);
    }

    void addLocalAttribute(BLELocalAttribute& localAttribute) {
      if (this->_localAttributes == NULL) {
        this->initLocalAttributes();
      }

      this->_localAttributes[this->_numLocalAttributes] = &localAttribute;
      this->_numLocalAttributes++;
    }

    void addRemoteAttribute(BLERemoteAttribute& remoteAttribute) {
      if (this->_remoteAttributes == NULL) {
        this->_remoteAttributes = (BLERemoteAttribute**)malloc(BLERemoteAttribute::numAttributes() * sizeof(BLERemoteAttribute*));
      }

      this->_remoteAttributes[this->_numRemoteAttributes] = &remoteAttribute;
      this->_numRemoteAttributes++;
    }



    void disconnect() {
      this->_device->disconnect();
    }


    BLECentral central() {
      this->poll();

      return this->_central;
    }

    bool connected() {
      this->poll();

      return this->_central;
    }

    void setEventHandler(BLEPeripheralEvent event, BLEPeripheralEventHandler eventHandler) {
      if (event < sizeof(this->_eventHandlers)) {
        this->_eventHandlers[event] = eventHandler;
      }
    }

  protected:
    bool characteristicValueChanged(BLECharacteristic& characteristic) {
      return this->_device->updateCharacteristicValue(characteristic);
    }

    bool broadcastCharacteristic(BLECharacteristic& characteristic) {
      return this->_device->broadcastCharacteristic(characteristic);
    }

    bool canNotifyCharacteristic(BLECharacteristic& characteristic) {
      return this->_device->canNotifyCharacteristic(characteristic);
    }

    bool canIndicateCharacteristic(BLECharacteristic& characteristic) {
      return this->_device->canIndicateCharacteristic(characteristic);
    }

    bool canReadRemoteCharacteristic(BLERemoteCharacteristic& characteristic) {
      return this->_device->canReadRemoteCharacteristic(characteristic);
    }

    bool readRemoteCharacteristic(BLERemoteCharacteristic& characteristic) {
      return this->_device->readRemoteCharacteristic(characteristic);
    }

    bool canWriteRemoteCharacteristic(BLERemoteCharacteristic& characteristic) {
      return this->_device->canWriteRemoteCharacteristic(characteristic);
    }

    bool writeRemoteCharacteristic(BLERemoteCharacteristic& characteristic, const unsigned char value[], unsigned char length) {
      return this->_device->writeRemoteCharacteristic(characteristic, value, length);
    }

    bool canSubscribeRemoteCharacteristic(BLERemoteCharacteristic& characteristic) {
      return this->_device->canSubscribeRemoteCharacteristic(characteristic);
    }

    bool subscribeRemoteCharacteristic(BLERemoteCharacteristic& characteristic) {
      return this->_device->subscribeRemoteCharacteristic(characteristic);
    }

    bool canUnsubscribeRemoteCharacteristic(BLERemoteCharacteristic& characteristic) {
      return this->_device->canUnsubscribeRemoteCharacteristic(characteristic);
    }

    bool unsubcribeRemoteCharacteristic(BLERemoteCharacteristic& characteristic) {
      return this->_device->unsubcribeRemoteCharacteristic(characteristic);
    }


    virtual void BLEDeviceConnected(BLEDevice& /*device*/, const unsigned char* address) {
      this->_central.setAddress(address);

    #ifdef BLE_PERIPHERAL_DEBUG
      Serial.print(F("Peripheral connected to central: "));
      Serial.println(this->_central.address());
    #endif

      BLEPeripheralEventHandler eventHandler = this->_eventHandlers[BLEConnected];
      if (eventHandler) {
        eventHandler(this->_central);
      }
    }

    virtual  void BLEDeviceDisconnected(BLEDevice& /*device*/) {
    #ifdef BLE_PERIPHERAL_DEBUG
      Serial.print(F("Peripheral disconnected from central: "));
      Serial.println(this->_central.address());
    #endif

      BLEPeripheralEventHandler eventHandler = this->_eventHandlers[BLEDisconnected];
      if (eventHandler) {
        eventHandler(this->_central);
      }

      this->_central.clearAddress();
    }

    virtual   void BLEDeviceBonded(BLEDevice& /*device*/) {
    #ifdef BLE_PERIPHERAL_DEBUG
      Serial.print(F("Peripheral bonded: "));
      Serial.println(this->_central.address());
    #endif

      BLEPeripheralEventHandler eventHandler = this->_eventHandlers[BLEBonded];
      if (eventHandler) {
        eventHandler(this->_central);
      }
    }

    virtual   void BLEDeviceRemoteServicesDiscovered(BLEDevice& /*device*/) {
    #ifdef BLE_PERIPHERAL_DEBUG
      Serial.print(F("Peripheral discovered central remote services: "));
      Serial.println(this->_central.address());
    #endif

      BLEPeripheralEventHandler eventHandler = this->_eventHandlers[BLERemoteServicesDiscovered];
      if (eventHandler) {
        eventHandler(this->_central);
      }
    }

    virtual  void BLEDeviceCharacteristicValueChanged(BLEDevice& /*device*/, BLECharacteristic& characteristic, const unsigned char* value, unsigned char valueLength) {
      characteristic.setValue(this->_central, value, valueLength);
    }

    virtual void BLEDeviceCharacteristicSubscribedChanged(BLEDevice& /*device*/, BLECharacteristic& characteristic, bool subscribed) {
      characteristic.setSubscribed(this->_central, subscribed);
    }

    virtual  void BLEDeviceRemoteCharacteristicValueChanged(BLEDevice& /*device*/, BLERemoteCharacteristic& remoteCharacteristic, const unsigned char* value, unsigned char valueLength) {
      remoteCharacteristic.setValue(this->_central, value, valueLength);
    }

    virtual  void BLEDeviceAddressReceived(BLEDevice& /*device*/, const unsigned char* /*address*/) {
    #ifdef BLE_PERIPHERAL_DEBUG
      char addressStr[18];

      BLEUtil::addressToString(address, addressStr);

      Serial.print(F("Peripheral address: "));
      Serial.println(addressStr);
    #endif
    }

    virtual  void BLEDeviceTemperatureReceived(BLEDevice& /*device*/, float /*temperature*/) {
    }

    virtual   void BLEDeviceBatteryLevelReceived(BLEDevice& /*device*/, float /*batteryLevel*/) {
    }


  private:
    void initLocalAttributes() {
      this->_localAttributes = (BLELocalAttribute**)malloc(BLELocalAttribute::numAttributes() * sizeof(BLELocalAttribute*));

      this->_localAttributes[0] = &this->_genericAccessService;
      this->_localAttributes[1] = &this->_deviceNameCharacteristic;
      this->_localAttributes[2] = &this->_appearanceCharacteristic;

      this->_localAttributes[3] = &this->_genericAttributeService;
      this->_localAttributes[4] = &this->_servicesChangedCharacteristic;

      this->_numLocalAttributes = 5;
    }

};


