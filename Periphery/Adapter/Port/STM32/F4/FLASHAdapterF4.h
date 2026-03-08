#pragma once
#include <Adapter/FLASHAdapter.h>


using AFLASH = class FLASHAdapterF4;

class FLASHAdapterF4: public FLASHAdapter<FLASH_TypeDef> {
protected:
	static constexpr uint32 FLASH_KEY1 = 0x45670123UL;
	static constexpr uint32 FLASH_KEY2 = 0xCDEF89ABUL;
	
	// Option Bytes keys for STM32F4
	static constexpr uint32 OPT_KEY1 = 0x08192A3BUL;
	static constexpr uint32 OPT_KEY2 = 0x4C5D6E7FUL;
	
	// Program size for STM32F4
	static constexpr uint32 FLASH_PSIZE_BYTE = 0x00000000UL;
	static constexpr uint32 FLASH_PSIZE_HALF_WORD = 0x00000100UL;
	static constexpr uint32 FLASH_PSIZE_WORD = 0x00000200UL;
	static constexpr uint32 FLASH_PSIZE_DOUBLE_WORD = 0x00000300UL;

public:
	FLASHAdapterF4() { }
	FLASHAdapterF4(FLASH_TypeDef *flash) : FLASHAdapter(flash) { }

	virtual ResultStatus Unlock(uint32 key1 = FLASH_KEY1, uint32 key2 = FLASH_KEY2) override {
		if (IsLocked()) {
			FLASH->KEYR = key1;
			FLASH->KEYR = key2;
			if (IsLocked()) {
				return ResultStatus::error;
			}
		}
		return ResultStatus::ok;
	}

	virtual ResultStatus Lock() override {
		FLASH->CR |= FLASH_CR_LOCK;
		return ResultStatus::ok;
	}

	virtual Result<uint8> Read(uint8 *address) override {
		if (!IsFlashArea((uint32)address, sizeof(*address))) {
			return Result<uint8>(ResultStatus::error, 0);
		}

		return Result<uint8>(ResultStatus::ok, *address);
	}

	virtual ResultStatus Write(uint16 *address, uint16 data) override {
		if (!IsFlashArea((uint32)address, sizeof(*address))) {
			return ResultStatus::error;
		}

		if (IsLocked()) {
			return ResultStatus::error;
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

	virtual ResultStatus WriteData(uint32 *address, const void *data, size_t size) override {
		if (!IsFlashArea((uint32)address, size)) {
			return ResultStatus::error;
		}

		if (IsLocked()) {
			return ResultStatus::error;
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
			if (GetStatus() != ResultStatus::ok) {
				return ResultStatus::error;
			}
		}

		return ResultStatus::ok;
	}

	virtual ResultStatus PageErase(uint8 *address) override {
		return SectorErase(GetSector((uint32)address));
	}

	virtual ResultStatus SectorErase(uint32 sectorNumber) override {
		if (sectorNumber > 11) {
			return ResultStatus::error;
		}

		if (IsLocked()) {
			return ResultStatus::error;
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

	virtual ResultStatus MassErase() override {
		if (IsLocked()) {
			return ResultStatus::error;
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

	virtual ResultStatus GetStatus() override {
		uint32 sr = READ_REG(FLASH->SR);
		if (sr & (FLASH_SR_WRPERR | FLASH_SR_PGAERR | FLASH_SR_PGPERR | FLASH_SR_PGSERR)) {
			return ResultStatus::error;
		}
		return ResultStatus::ok;
	}

	virtual ResultStatus ClearStatusFlags() override {
		SET_BIT(FLASH->SR, FLASH_SR_EOP | FLASH_SR_WRPERR | FLASH_SR_PGAERR | FLASH_SR_PGPERR | FLASH_SR_PGSERR);
		return ResultStatus::ok;
	}

	virtual Result<uint32> ReadOptionBytes() override {
		// Option Bytes can be read directly from OPTCR register
		// Values are already available in FLASH->OPTCR
		return static_cast<uint32>(FLASH->OPTCR);
	}

	virtual ResultStatus WriteOptionBytes(uint32 optionBytes) override {
		// Unlock Option Bytes
		ResultStatus status = UnlockOptionBytes();
		if (status != ResultStatus::ok) {
			return status;
		}

		// Write new option bytes value
		FLASH->OPTCR = optionBytes;

		// Start programming
		FLASH->OPTCR |= FLASH_OPTCR_OPTSTRT;

		// Wait for completion
		while (IsBusy());

		// Lock Option Bytes
		LockOptionBytes();

		return GetStatus();
	}


	virtual bool IsReadProtected() const override {
		uint8 rdpLevel = GetReadProtectionLevel();
		return rdpLevel != 0xAA;  // Level 0 = no protection
	}

	virtual uint8 GetReadProtectionLevel() const override {
		// RDP level is located in bits 8-15 of OPTCR register
		return (FLASH->OPTCR >> 8) & 0xFF;
	}

	virtual FlashProtectionLevel GetProtectionLevel() const override {
		uint8 rdpLevel = GetReadProtectionLevel();
		
		// STM32F4 specific RDP level interpretation
		if (rdpLevel == 0xAA) {
			return FlashProtectionLevel::Level0;
		} else if (rdpLevel == 0xCC) {
			return FlashProtectionLevel::Level2;
		} else {
			// For STM32F4: any other value means Level 1
			return FlashProtectionLevel::Level1;
		}
	}

	virtual ResultStatus SetReadProtectionLevel(FlashProtectionLevel level) override {
		uint8 rdpValue;
		
		// Convert abstract level to STM32F4 specific values
		switch (level) {
			case FlashProtectionLevel::Level0:
				rdpValue = 0xAA;
				break;
			case FlashProtectionLevel::Level1:
				rdpValue = 0x55;  // Any value except 0xAA and 0xCC
				break;
			case FlashProtectionLevel::Level2:
				rdpValue = 0xCC;
				break;
			default:
				return ResultStatus::invalidParameter;
		}

		// Unlock Option Bytes
		ResultStatus status = UnlockOptionBytes();
		if (status != ResultStatus::ok) {
			return status;
		}

		// Set new RDP level
		uint32 optcr = FLASH->OPTCR;
		optcr &= ~(0xFF << 8);     // Clear RDP bits
		optcr |= (rdpValue << 8);  // Set new level
		FLASH->OPTCR = optcr;

		// Start programming
		FLASH->OPTCR |= FLASH_OPTCR_OPTSTRT;

		// Wait for completion
		while (IsBusy());

		// Lock Option Bytes
		LockOptionBytes();

		return GetStatus();
	}

	virtual ResultStatus DisableReadProtection() override {
		// Remove RDP (set Level 0)
		return SetReadProtectionLevel(FlashProtectionLevel::Level0);
	}

	// Additional convenience methods for RDP level checking
	bool IsRDPLevel0() const {
		return GetReadProtectionLevel() == 0xAA;
	}

	bool IsRDPLevel1() const {
		uint8 level = GetReadProtectionLevel();
		return level != 0xAA && level != 0xCC;
	}

	bool IsRDPLevel2() const {
		return GetReadProtectionLevel() == 0xCC;
	}

	// Option Bytes methods (public implementation)
	virtual ResultStatus UnlockOptionBytes() override {
		if (IsOptionBytesLocked()) {
			FLASH->OPTKEYR = OPT_KEY1;
			FLASH->OPTKEYR = OPT_KEY2;
			if (IsOptionBytesLocked()) {
				return ResultStatus::error;
			}
		}
		return ResultStatus::ok;
	}

	virtual ResultStatus LockOptionBytes() override {
		FLASH->OPTCR |= FLASH_OPTCR_OPTLOCK;
		return ResultStatus::ok;
	}

	virtual bool IsOptionBytesLocked() const override {
		return (FLASH->OPTCR & FLASH_OPTCR_OPTLOCK) != 0;
	}

protected:
	virtual ResultStatus Initialization() override {
		auto status = BeforeInitialization();
		if (status != ResultStatus::ok) {
			return status;
		}

		return AfterInitialization();
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
