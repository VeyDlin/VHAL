#pragma once
#include <BSP.h>
#include "MemoryAccessPort.h"


// STM32 -> MemoryAccessPort -> DebugPort -> SWD
class STM32 {
	static const uint32 FLASH_BASE_REGION = 0x40022000;

	static const uint32 FLASH_KEYR = FLASH_BASE_REGION + 0x04;
	static const uint32 FLASH_SR = FLASH_BASE_REGION + 0x0c;
	static const uint32 FLASH_CR = FLASH_BASE_REGION + 0x10;
	static const uint32 FLASH_AR = FLASH_BASE_REGION + 0x14;


private:
	MemoryAccessPort memoryAccessPort;


public:
	STM32(AGPIO &clock, AGPIO &data, uint32 frequency, uint32 apSel) {
		memoryAccessPort = MemoryAccessPort(clock, data, frequency, apSel);
	}


	void halt() {
		memoryAccessPort.writeWord(0xE000EDF0, 0xA05F0003);
	}


	void unhalt() {
		memoryAccessPort.writeWord(0xE000EDF0, 0xA05F0000);
	}


	void reset() {
		memoryAccessPort.writeWord(0xE000ED0C, 0x05FA0004);
	}


	void unlockFlash() {
		memoryAccessPort.writeWord(FLASH_KEYR, 0x45670123);
		memoryAccessPort.writeWord(FLASH_KEYR, 0xcdef89ab);
	}


	void lockFlash() {
		memoryAccessPort.writeWord(FLASH_CR, memoryAccessPort.readWord(FLASH_CR) | (1 << 7));
	}


	void startProgramming() {
		memoryAccessPort.writeWord(FLASH_CR, 1);
	}


	void endProgramming() {
		memoryAccessPort.writeWord(FLASH_CR, 0);
	}


	void eraseFlash(uint32_t page) {
		memoryAccessPort.writeWord(FLASH_CR, 2);
		memoryAccessPort.writeWord(FLASH_AR, page);
		memoryAccessPort.writeWord(FLASH_CR, 0x42);
		while ((memoryAccessPort.readWord(FLASH_SR) & 0x1) != 0) {
			System::DelayUs(10000);
		}
	}


	void eraseFlash() {
		// TODO
	}


	void waitFlash() {
		while ((memoryAccessPort.readWord(FLASH_SR) & 0x1) != 0) {
			System::DelayUs(10000);
		}
	}


	MemoryAccessPort *getMemoryAccessPort() {
		return &memoryAccessPort;
	}

};


