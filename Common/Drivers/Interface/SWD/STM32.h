#pragma once
#include <BSP.h>
#include "Core/MemoryAccessPort.h"


/*
	auto stm32 = STM32(BSP::swc, BSP::swd);
	stm32.Init();

	auto dpIdCode = stm32.GetDebugPortIdCode();
	auto apIdCode = stm32.getIdCode();
	auto mcuIdCode = stm32.ReadWord(0x40015800);


	// Reading program file
	char buffer[1 << 16];
	// ...

	// Halting the processor
	stm32.Halt();

	// Unlocking the program memory
	stm32.UnlockFlash();

	// Erasing previous content
	for (uint32 i = 0; i < ((size + 0x3ff) & ~0x3ff); i += 0x400) {
		stm32.EraseFlash(0x08000000 + i);
		for (uint32 j = 0; j < 0x400; j += 4) {
			uint32 erase = stm32.ReadWord(0x08000000 + i + j);
			if (erase != 0xffffffff) {
				// ! Erasing failed at 0x08000000 + i + j
			}
		}
	}

	// Programming
	stm32.StartProgramming();
	for (uint32 i = 0; i < size; i += 4) {
		stm32.WriteHalf(0x08000000 + i, *(uint32*)(buffer + i));
		stm32.WaitFlash();
		uint32 value = stm32.ReadWord(0x08000000 + i);
		if (value != *(uint32*)(buffer + i)) {
			// ! Programming failed at 0x08000000 + i
			// ! Difference:  *(uint32*)(buffer + i) /  value
		}
	}

	//stm32.WriteHalFS(0x08000000, size >> 2, (uint32*)buffer);
	stm32.EndProgramming();

	// Verifying memory content
	char *download = new char[size];
	//stm32.ReadBlock(0x08000000, size >> 2, (uint32*)download);
	for (uint32 i = 0; i < size >> 2; i++) {
		((uint32*)download)[i] = stm32.ReadWord(0x08000000 + (i << 2));
	}
	for (uint32 i = 0; i < size; i++) {
		if (buffer[i] != download[i]) {
			// ! Error at byte i: buffer[i] / download[i]
		}
	}
	delete[] download;

	// Resetting the processor
	stm32.UnHalt();
	stm32.Reset();
*/


class STM32 : public MemoryAccessPort {
	static const uint32 FLASH_BASE_REGION = 0x40022000;

	static const uint32 FLASH_KEYR = FLASH_BASE_REGION + 0x04;
	static const uint32 FLASH_SR = FLASH_BASE_REGION + 0x0c;
	static const uint32 FLASH_CR = FLASH_BASE_REGION + 0x10;
	static const uint32 FLASH_AR = FLASH_BASE_REGION + 0x14;


public:
	STM32(AGPIO& clock, AGPIO& data, uint32 frequency = 50000, uint32 apSel = 0) :
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
		// TODO: Add EraseFlash
	}


	void WaitFlash() {
		while ((ReadWord(FLASH_SR) & 0x1) != 0) {
			System::DelayUs(10000);
		}
	}
};
