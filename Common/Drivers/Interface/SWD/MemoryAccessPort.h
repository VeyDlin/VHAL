#pragma once
#include <BSP.h>
#include "DebugPort.h"


class MemoryAccessPort {
private:
	DebugPort debugPort;
	uint32 apSel;


public:
	MemoryAccessPort() {}

	MemoryAccessPort(AGPIO &clock, AGPIO &data, uint32 frequency, uint32 apSel): apSel(apSel) {
		debugPort = DebugPort(clock, data, frequency);
		csw(1, 2);
	}





	uint32_t getIdCode() {
		debugPort.readAP(apSel, 0xfc);
		return debugPort.readRB();
	}





	uint32_t readWord(uint32_t address) {
		debugPort.writeAP(apSel, 0x04, address);
		debugPort.readAP(apSel, 0x0c);
		return debugPort.readRB();
	}





	uint32_t writeWord(uint32_t address, uint32_t value) {
		debugPort.writeAP(apSel, 0x04, address);
		debugPort.writeAP(apSel, 0x0c, value);
		return debugPort.readRB();
	}





	uint32_t readHalf(uint32_t address) {
		csw(0, 1);
		debugPort.writeAP(apSel, 0x04, address);
		debugPort.readAP(apSel, 0x0c);
		csw(1, 2);
		return debugPort.readRB();
	}





	uint32_t writeHalf(uint32_t address, uint32_t value) {
		csw(1, 1);
		debugPort.writeAP(apSel, 0x04, address);
		debugPort.writeAP(apSel, 0x0c, value);
		debugPort.writeAP(apSel, 0x0c, value);
		csw(1, 2);
		return debugPort.readRB();
	}





	void readBlock(uint32_t address,  uint32_t count, uint32_t *buffer) {
		debugPort.writeAP(apSel, 0x04, address);
		debugPort.readAP(apSel, 0x0c);
		for (unsigned int i = 0; i < count - 1; i++) {
			buffer[i] = debugPort.readAP(apSel, 0x0c);
		}
		buffer[count - 1] = debugPort.readRB();
	}





	void writeBlock(uint32_t address, uint32_t count, const uint32_t *buffer) {
		debugPort.writeAP(apSel, 0x04, address);
		for (unsigned int i = 0; i < count; i++) {
			debugPort.writeAP(apSel, 0x0c, buffer[i]);
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
		debugPort.writeAP(apSel, 0x04, address);
		for (unsigned int i = 0; i < count; i++) {
			debugPort.writeAP(apSel, 0x0c, buffer[i]);
			debugPort.writeAP(apSel, 0x0c, buffer[i]);
		}
		/*// 16-bit packed-incrementing addressing
		csw(2, 1);
		writeBlock(address, count, buffer);*/
		// 32-bit auto-incrementing addressing
		csw(1, 2);
	}


private:
	void csw(unsigned int addrInc, unsigned int size) {
		debugPort.readAP(apSel, 0x00);
		uint32_t csw = debugPort.readRB() & 0xFFFFFF00;
		debugPort.writeAP(apSel, 0x00, csw + (addrInc << 4) + size);
	}

};



