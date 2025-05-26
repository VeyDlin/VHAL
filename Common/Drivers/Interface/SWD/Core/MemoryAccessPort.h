#pragma once
#include <BSP.h>
#include "DebugPort.h"


class MemoryAccessPort : public DebugPort {
private:
	uint32 apSel;

public:
	MemoryAccessPort() {}

	MemoryAccessPort(AGPIO& clock, AGPIO& data, uint32 frequency, uint32 apSel) :
		DebugPort(clock, data, frequency), apSel(apSel)
	{

	}


	void Init() {
		DebugPort::Init();
		CSW(1, 2);
	}


	uint32 GetIdCode() {
		ReadAP(apSel, 0xfc);
		return ReadRB();
	}


	uint32 ReadWord(uint32 address) {
		WriteAP(apSel, 0x04, address);
		ReadAP(apSel, 0x0c);
		return ReadRB();
	}


	uint32 WriteWord(uint32 address, uint32 value) {
		WriteAP(apSel, 0x04, address);
		WriteAP(apSel, 0x0c, value);
		return ReadRB();
	}


	uint32 ReadHalf(uint32 address) {
		CSW(0, 1);
		WriteAP(apSel, 0x04, address);
		ReadAP(apSel, 0x0c);
		CSW(1, 2);
		return ReadRB();
	}


	uint32 WriteHalf(uint32 address, uint32 value) {
		CSW(1, 1);
		WriteAP(apSel, 0x04, address);
		WriteAP(apSel, 0x0c, value);
		WriteAP(apSel, 0x0c, value);
		CSW(1, 2);
		return ReadRB();
	}


	void ReadBlock(uint32 address, uint32 count, uint32* buffer) {
		WriteAP(apSel, 0x04, address);
		ReadAP(apSel, 0x0c);
		for (unsigned int i = 0; i < count - 1; i++) {
			buffer[i] = ReadAP(apSel, 0x0c);
		}
		buffer[count - 1] = ReadRB();
	}


	void WriteBlock(uint32 address, uint32 count, const uint32* buffer) {
		WriteAP(apSel, 0x04, address);
		for (unsigned int i = 0; i < count; i++) {
			WriteAP(apSel, 0x0c, buffer[i]);
		}
	}


	void WriteBlockNonInc(uint32 address, uint32 count, const uint32* buffer) {
		// 32-bit non-incrementing addressing
		CSW(0, 2);

		WriteBlock(address, count, buffer);

		// 32-bit auto-incrementing addressing
		CSW(1, 2);
	}


	void WriteHalFS(uint32 address, uint32 count, const uint32* buffer) {
		// 16-bit auto-incrementing addressing
		CSW(1, 1);

		WriteAP(apSel, 0x04, address);
		for (unsigned int i = 0; i < count; i++) {
			WriteAP(apSel, 0x0c, buffer[i]);
			WriteAP(apSel, 0x0c, buffer[i]);
		}
		/*
			// 16-bit packed-incrementing addressing
			CSW(2, 1);
			WriteBlock(address, count, buffer);
		*/

		// 32-bit auto-incrementing addressing
		CSW(1, 2);
	}


private:
	void CSW(unsigned int addrInc, unsigned int size) {
		ReadAP(apSel, 0x00);
		uint32 csw = ReadRB() & 0xFFFFFF00;
		WriteAP(apSel, 0x00, csw + (addrInc << 4) + size);
	}
};
