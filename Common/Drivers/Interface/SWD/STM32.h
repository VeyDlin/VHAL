#pragma once
#include <BSP.h>
#include "MemoryAccessPort.h"


// STM32 -> MemoryAccessPort -> DebugPort -> SWD
class STM32 : public MemoryAccessPort {
	static const uint32 FLASH_BASE_REGION = 0x40022000;

	static const uint32 FLASH_KEYR = FLASH_BASE_REGION + 0x04;
	static const uint32 FLASH_SR = FLASH_BASE_REGION + 0x0c;
	static const uint32 FLASH_CR = FLASH_BASE_REGION + 0x10;
	static const uint32 FLASH_AR = FLASH_BASE_REGION + 0x14;


public:
        STM32(AGPIO &clock, AGPIO &data, uint32 frequency, uint32 apSel) :
                MemoryAccessPort(clock, data, frequency, apSel) {
        }


	void halt() {
                writeWord(0xE000EDF0, 0xA05F0003);
	}


	void unhalt() {
                writeWord(0xE000EDF0, 0xA05F0000);
	}


	void reset() {
                writeWord(0xE000ED0C, 0x05FA0004);
	}


	void unlockFlash() {
                writeWord(FLASH_KEYR, 0x45670123);
                writeWord(FLASH_KEYR, 0xcdef89ab);
	}


	void lockFlash() {
                writeWord(FLASH_CR, readWord(FLASH_CR) | (1 << 7));
	}


	void startProgramming() {
                writeWord(FLASH_CR, 1);
	}


	void endProgramming() {
                writeWord(FLASH_CR, 0);
	}


	void eraseFlash(uint32_t page) {
                writeWord(FLASH_CR, 2);
                writeWord(FLASH_AR, page);
                writeWord(FLASH_CR, 0x42);
                while ((readWord(FLASH_SR) & 0x1) != 0) {
                        System::DelayUs(10000);
                }
        }


	void eraseFlash() {
		// TODO
	}


	void waitFlash() {
                while ((readWord(FLASH_SR) & 0x1) != 0) {
                        System::DelayUs(10000);
                }
        }


	MemoryAccessPort *getMemoryAccessPort() {
                return this;
	}

};


