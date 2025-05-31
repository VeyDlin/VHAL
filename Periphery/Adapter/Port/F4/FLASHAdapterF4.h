#pragma once
#include "../../FLASHAdapter.h"
#include <System/System.h>
#include <stm32f4xx.h>

using AFLASH = class FLASHAdapterF4;

class FLASHAdapterF4: public FLASHAdapter {
protected:
	static constexpr uint32_t FLASH_KEY1 = 0x45670123UL;
	static constexpr uint32_t FLASH_KEY2 = 0xCDEF89ABUL;
	
	// Program size for STM32F4
	static constexpr uint32_t FLASH_PSIZE_BYTE = 0x00000000UL;
	static constexpr uint32_t FLASH_PSIZE_HALF_WORD = 0x00000100UL;
	static constexpr uint32_t FLASH_PSIZE_WORD = 0x00000200UL;
	static constexpr uint32_t FLASH_PSIZE_DOUBLE_WORD = 0x00000300UL;

public:
	FLASHAdapterF4() { }
	FLASHAdapterF4(FLASH_TypeDef *flash) : FLASHAdapter(flash) { }

	virtual Status::statusType Unlock(uint32 key1 = FLASH_KEY1, uint32 key2 = FLASH_KEY2) override {
		if (IsLocked()) {
			FLASH->KEYR = key1;
			FLASH->KEYR = key2;
			if (IsLocked()) {
				return Status::error;
			}
		}
		return Status::ok;
	}

	virtual Status::statusType Lock() override {
		FLASH->CR |= FLASH_CR_LOCK;
		return Status::ok;
	}

	virtual Status::info<uint8> Read(uint8 *address) override {
		if (!IsFlashArea((uint32)address, sizeof(*address))) {
			return Status::info<uint8>(Status::error, 0);
		}

		return Status::info<uint8>(Status::ok, *address);
	}

	virtual Status::statusType Write(uint16 *address, uint16 data) override {
		if (!IsFlashArea((uint32)address, sizeof(*address))) {
			return Status::error;
		}

		if (IsLocked()) {
			return Status::error;
		}

		while (IsBusy());

		ClearStatusFlags();
		FLASH->CR &= ~(FLASH_CR_PSIZE);
		FLASH->CR |= FLASH_PSIZE_HALF_WORD;
		FLASH->CR |= FLASH_CR_PG;
		*address = data;

		while (IsBusy());
		FLASH->CR &= ~FLASH_CR_PG;

		return GetStatus();
	}

	virtual Status::statusType WriteData(uint32 *address, const void *data, size_t size) override {
		if (!IsFlashArea((uint32)address, size)) {
			return Status::error;
		}

		if (IsLocked()) {
			return Status::error;
		}

		const uint32 *src = static_cast<const uint32*>(data);
		uint32 *dst = address;
		size_t wordCount = (size + 3) / 4;

		for (size_t i = 0; i < wordCount; i++) {
			while (IsBusy());
			ClearStatusFlags();
			FLASH->CR &= ~(FLASH_CR_PSIZE);
			FLASH->CR |= FLASH_PSIZE_WORD;
			FLASH->CR |= FLASH_CR_PG;
			*dst++ = *src++;
			while (IsBusy());
			FLASH->CR &= ~FLASH_CR_PG;
			if (GetStatus() != Status::ok) {
				return Status::error;
			}
		}

		return Status::ok;
	}

	virtual Status::statusType PageErase(uint8 *address) override {
		return SectorErase(GetSector((uint32)address));
	}

	virtual Status::statusType SectorErase(uint32 sectorNumber) override {
		if (sectorNumber > 11) {
			return Status::error;
		}

		if (IsLocked()) {
			return Status::error;
		}

		while (IsBusy());

		ClearStatusFlags();
		FLASH->CR &= ~(FLASH_CR_PSIZE | FLASH_CR_SNB);
		FLASH->CR |= FLASH_PSIZE_WORD;
		FLASH->CR |= FLASH_CR_SER | (sectorNumber << FLASH_CR_SNB_Pos);
		FLASH->CR |= FLASH_CR_STRT;

		while (IsBusy());
		FLASH->CR &= ~FLASH_CR_SER;

		return GetStatus();
	}

	virtual Status::statusType MassErase() override {
		if (IsLocked()) {
			return Status::error;
		}

		while (IsBusy());

		ClearStatusFlags();
		FLASH->CR &= ~FLASH_CR_PSIZE;
		FLASH->CR |= FLASH_CR_MER | FLASH_PSIZE_WORD;
		FLASH->CR |= FLASH_CR_STRT;

		while (IsBusy());
		FLASH->CR &= ~FLASH_CR_MER;

		return GetStatus();
	}

	virtual Status::statusType GetStatus() override {
		uint32 sr = READ_REG(FLASH->SR);
		if (sr & (FLASH_SR_WRPERR | FLASH_SR_PGAERR | FLASH_SR_PGPERR | FLASH_SR_PGSERR)) {
			return Status::error;
		}
		return Status::ok;
	}

	virtual Status::statusType ClearStatusFlags() override {
		SET_BIT(FLASH->SR, FLASH_SR_EOP | FLASH_SR_WRPERR | FLASH_SR_PGAERR | FLASH_SR_PGPERR | FLASH_SR_PGSERR);
		return Status::ok;
	}

	virtual Status::statusType ReadOptionBytes() override {
		return Status::notImplemented;
	}

	virtual Status::statusType WriteOptionBytes() override {
		return Status::notImplemented;
	}

protected:
	virtual Status::statusType Initialization() override {
		return Status::ok;
	}

	bool IsFlashArea(uint32 address, uint32 size) {
		if ((address < parameters.startAddress) || (address + size > parameters.endAddress)) {
			return false;
		}
		return true;
	}

	bool IsLocked(void) {
		return (FLASH->CR & FLASH_CR_LOCK) != 0;
	}

	bool IsBusy(void) {
		return (FLASH->SR & FLASH_SR_BSY) != 0;
	}

	uint32 GetSector(uint32 address) {
		// STM32F412 has different sector sizes:
		// Sector 0-3: 16KB each (0x08000000 - 0x0800FFFF)
		// Sector 4: 64KB (0x08010000 - 0x0801FFFF)
		// Sector 5-7: 128KB each (0x08020000 - 0x0807FFFF)
		
		if (address < 0x08004000) return 0;  // 16KB
		if (address < 0x08008000) return 1;  // 16KB
		if (address < 0x0800C000) return 2;  // 16KB
		if (address < 0x08010000) return 3;  // 16KB
		if (address < 0x08020000) return 4;  // 64KB
		if (address < 0x08040000) return 5;  // 128KB
		if (address < 0x08060000) return 6;  // 128KB
		if (address < 0x08080000) return 7;  // 128KB
		return 0xFF; // Invalid sector
	}
};
