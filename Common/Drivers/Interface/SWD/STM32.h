#pragma once
#include <BSP.h>
#include "Core/MemoryAccessPort.h"


class STM32 : public MemoryAccessPort {
	static const uint32 FLASH_BASE_REGION = 0x40022000;

	static const uint32 FLASH_KEYR = FLASH_BASE_REGION + 0x04;
	static const uint32 FLASH_SR = FLASH_BASE_REGION + 0x0c;
	static const uint32 FLASH_CR = FLASH_BASE_REGION + 0x10;
	static const uint32 FLASH_AR = FLASH_BASE_REGION + 0x14;


public:
	STM32(AGPIO& clock, AGPIO& data, uint32 frequency, uint32 apSel) :
		MemoryAccessPort(clock, data, frequency, apSel) 
	{

	}


	void Init() {
		MemoryAccessPort::Init();
	}


	void Halt() {
		WriteWord(0xE000EDF0, 0xA05F0003);
	}


	void UnHalt() {
		WriteWord(0xE000EDF0, 0xA05F0000);
	}


	void Reset() {
		WriteWord(0xE000ED0C, 0x05FA0004);
	}


	void UnlockFlash() {
		WriteWord(FLASH_KEYR, 0x45670123);
		WriteWord(FLASH_KEYR, 0xcdef89ab);
	}


	void LockFlash() {
		WriteWord(FLASH_CR, ReadWord(FLASH_CR) | (1 << 7));
	}


	void StartProgramming() {
		WriteWord(FLASH_CR, 1);
	}


	void EndProgramming() {
		WriteWord(FLASH_CR, 0);
	}


	void EraseFlash(uint32_t page) {
		WriteWord(FLASH_CR, 2);
		WriteWord(FLASH_AR, page);
		WriteWord(FLASH_CR, 0x42);
		while ((ReadWord(FLASH_SR) & 0x1) != 0) {
			System::DelayUs(10000);
		}
	}


	void EraseFlash() {
		// TODO
	}


	void WaitFlash() {
		while ((ReadWord(FLASH_SR) & 0x1) != 0) {
			System::DelayUs(10000);
		}
	}
};
