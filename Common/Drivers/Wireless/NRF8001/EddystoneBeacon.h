#pragma once
#include "BLEPeripheral.h"
#include "BLEUuid.h"
#include <string>

#define MAX_SERVICE_DATA_SIZE 20

#define FLAGS_UID 0x00
#define FLAGS_URL 0x10
#define FLAGS_TLM 0x20


class EddystoneBeacon: public BLEPeripheral {
private:
	static constexpr char* EDDYSTONE_URL_BEACON_PREFIX_SUBSTITUTIONS[] = {
		"http://www.",
		"https://www.",
		"http://",
		"https://",
		"urn:uuid:"
	};

	static constexpr char* EDDYSTONE_URL_BEACON_SUFFIX_SUBSTITUTIONS[] = {
		".com/",
		".org/",
		".edu/",
		".net/",
		".info/",
		".biz/",
		".gov/",
		".com",
		".org",
		".edu",
		".net",
		".info",
		".biz",
		".gov"
	};


public:
	 EddystoneBeacon(ASPI *spi, AGPIO *req, AGPIO *rdy, AGPIO *rst) :
			BLEPeripheral(spi, req, rdy, rst), _bleService("feaa"),
			_bleCharacteristic("feab", BLERead | BLEBroadcast, MAX_SERVICE_DATA_SIZE)
	{
		this->setConnectable(false);
		this->addAttribute(this->_bleService);
		this->addAttribute(this->_bleCharacteristic);
	}


	void  begin(char power, const BLEUuid &uid) {
		unsigned char serviceData[MAX_SERVICE_DATA_SIZE];

		this->_power = power;

		memset(serviceData, 0x00, sizeof(serviceData));
		serviceData[0] = FLAGS_UID;
		serviceData[1] = this->_power;

		const unsigned char *uidData = uid.data();
		for (int i = 0; i < 16; i++) {
			serviceData[i + 2] = uidData[15 - i];
		}

		serviceData[18] = 0x00; // Reserved for future use, must be: 0x00
		serviceData[19] = 0x00; // Reserved for future use, must be: 0x00

		this->_bleCharacteristic.setValue(serviceData, sizeof(serviceData));

		this->setAdvertisedServiceUuid(this->_bleService.uuid());

		BLEPeripheral::begin();

		this->_bleCharacteristic.broadcast();
	}

	void  begin(char power, const char *uri) {
		this->_power = power;
		this->setURI(uri);

		this->setAdvertisedServiceUuid(this->_bleService.uuid());

		BLEPeripheral::begin();

		this->_bleCharacteristic.broadcast();
	}

	void  loop() {
		this->poll();
	}

	void  setURI(const char *uri) {
		unsigned char serviceData[MAX_SERVICE_DATA_SIZE];

		serviceData[0] = FLAGS_URL;
		serviceData[1] = this->_power;
		unsigned char compressedURIlength = this->compressURI(uri, (char*) &serviceData[2], sizeof(serviceData) - 2);

		this->_bleCharacteristic.setValue(serviceData, 2 + compressedURIlength);
	}


private:
	// TODO:: std string
	unsigned char  compressURI(const char *uri, char *compressedUri, unsigned char compressedUriSize) {
		std::string uriString = uri;

		// replace prefixes
		for (unsigned int i = 0; i < (sizeof(EDDYSTONE_URL_BEACON_PREFIX_SUBSTITUTIONS) / sizeof(char*)); i++) {
			std::string replacement = " ";
			replacement[0] = (char) (i | 0x80); // set high bit, String.replace does not like '\0' replacement

			//uriString.replace(EDDYSTONE_URL_BEACON_PREFIX_SUBSTITUTIONS[i], replacement);
			std::string toReplace = EDDYSTONE_URL_BEACON_PREFIX_SUBSTITUTIONS[i];
			uriString.replace(uriString.find(toReplace), toReplace.length(), replacement);
		}

		// replace suffixes
		for (unsigned int i = 0; i < (sizeof(EDDYSTONE_URL_BEACON_SUFFIX_SUBSTITUTIONS) / sizeof(char*)); i++) {
			std::string replacement = " ";
			replacement[0] = (char) (i | 0x80); // set high bit, String.replace does not like '\0' replacement

			//uriString.replace(EDDYSTONE_URL_BEACON_SUFFIX_SUBSTITUTIONS[i], replacement);
			std::string toReplace = EDDYSTONE_URL_BEACON_SUFFIX_SUBSTITUTIONS[i];
			uriString.replace(uriString.find(toReplace), toReplace.length(), replacement);
		}

		unsigned char i = 0;
		for (i = 0; i < uriString.length() && i < compressedUriSize; i++) {
			compressedUri[i] = (uriString[i] & 0x7f); // assign byte after clearing hight bit
		}

		return i;
	}
	char _power;

	BLEService _bleService;
	BLECharacteristic _bleCharacteristic;
};

