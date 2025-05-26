#pragma once
#include <BSP.h>
#include "DebugPort.h"


class MemoryAccessPort : public DebugPort {
private:
        uint32 apSel;


public:
        MemoryAccessPort() {}

        MemoryAccessPort(AGPIO &clock, AGPIO &data, uint32 frequency, uint32 apSel) :
                DebugPort(clock, data, frequency),
                apSel(apSel) {
                csw(1, 2);
        }





	uint32_t getIdCode() {
                readAP(apSel, 0xfc);
                return readRB();
	}





	uint32_t readWord(uint32_t address) {
                writeAP(apSel, 0x04, address);
                readAP(apSel, 0x0c);
                return readRB();
	}





	uint32_t writeWord(uint32_t address, uint32_t value) {
                writeAP(apSel, 0x04, address);
                writeAP(apSel, 0x0c, value);
                return readRB();
	}





	uint32_t readHalf(uint32_t address) {
		csw(0, 1);
                writeAP(apSel, 0x04, address);
                readAP(apSel, 0x0c);
                csw(1, 2);
                return readRB();
	}





	uint32_t writeHalf(uint32_t address, uint32_t value) {
		csw(1, 1);
                writeAP(apSel, 0x04, address);
                writeAP(apSel, 0x0c, value);
                writeAP(apSel, 0x0c, value);
                csw(1, 2);
                return readRB();
	}





	void readBlock(uint32_t address,  uint32_t count, uint32_t *buffer) {
                writeAP(apSel, 0x04, address);
                readAP(apSel, 0x0c);
		for (unsigned int i = 0; i < count - 1; i++) {
                        buffer[i] = readAP(apSel, 0x0c);
		}
                buffer[count - 1] = readRB();
	}





	void writeBlock(uint32_t address, uint32_t count, const uint32_t *buffer) {
                writeAP(apSel, 0x04, address);
		for (unsigned int i = 0; i < count; i++) {
                        writeAP(apSel, 0x0c, buffer[i]);
		}
	}





	void writeBlockNonInc(uint32_t address, uint32_t count, const uint32_t *buffer) {
		// 32-bit non-incrementing addressing
		csw(0, 2);
		writeBlock(address, count, buffer);
		// 32-bit auto-incrementing addressing
		csw(1, 2);
	}





	void writeHalfs(uint32_t address, uint32_t count, const uint32_t *buffer) {
		// 16-bit auto-incrementing addressing
		csw(1, 1);
                writeAP(apSel, 0x04, address);
		for (unsigned int i = 0; i < count; i++) {
                        writeAP(apSel, 0x0c, buffer[i]);
                        writeAP(apSel, 0x0c, buffer[i]);
		}
		/*// 16-bit packed-incrementing addressing
		csw(2, 1);
		writeBlock(address, count, buffer);*/
		// 32-bit auto-incrementing addressing
		csw(1, 2);
	}


private:
	void csw(unsigned int addrInc, unsigned int size) {
                readAP(apSel, 0x00);
                uint32_t csw = readRB() & 0xFFFFFF00;
                writeAP(apSel, 0x00, csw + (addrInc << 4) + size);
	}

};



