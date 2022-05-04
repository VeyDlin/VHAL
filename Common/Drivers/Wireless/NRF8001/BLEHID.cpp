#include <PortAdapters.h>
#ifdef AUSED_SPI_ADAPTER

#include "BLEHID.h"
#include "BLEHIDPeripheral.h"


unsigned char BLEHID::_numHids = 0;


void BLEHID::sendData(BLECharacteristic &characteristic, unsigned char data[], unsigned char length) {
	// wait until we can notify
	while (!characteristic.canNotify()) {
		BLEHIDPeripheral::instance()->poll();
	}

	characteristic.setValue(data, length);
}

#endif
