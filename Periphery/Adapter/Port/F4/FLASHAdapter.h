#pragma once
#include "../../FLASHAdapter.h"
#include <System/System.h>

using AFLASH = class FLASHAdapterF4;

class FLASHAdapterF4: public FLASHAdapter {
protected:
	uint32 flashStart;
	uint32 flashEnd;


public:
	FLASHAdapterF4() { }
	FLASHAdapterF4(FLASH_TypeDef *flash, uint32 start, uint32 end) : FLASHAdapter(flash, start, end) { }


	virtual void Unlock(uint32 kye1, uint32 key2) override {
		WRITE_REG(&flashHandle->KEYR, kye1);
		WRITE_REG(&flashHandle->KEYR, key2);
	}



	virtual Status::info<uint8> Read(uint8 *address) override {
		if (!IsFlashArea((uint32)address, sizeof(*address))) {
			return 0;
		}

		return *address;
	}



	virtual Status::statusType Write(uint16 *address, uint16 data) override {
		if (!IsFlashArea((uint32)address, sizeof(*address))) {
			return Status::error;
		}

		if (IsLocked()) {
			return Status::error;
		}

		while (IsBusy());

		SET_BIT(&flashHandle->CR, FLASH_CR_PG);
		*address = data;

		while (IsBusy());

		return CheckEop();
	}



	virtual Status::statusType PageErase(uint8 *address) override {
		if (!IsFlashArea((uint32)address, sizeof(*address))) {
			return Status::error;
		}

		if (IsLocked()) {
			return Status::error;
		}

		while (IsBusy());

		SET_BIT(&flashHandle->CR, FLASH_CR_PER);
		WRITE_REG(&flashHandle->AR, (uint32)address);
		SET_BIT(&flashHandle->CR, FLASH_CR_STRT);

		while (IsBusy());

		return CheckEop();
	}




protected:
	bool IsFlashArea(uint32 address, uint32 size) {
		if ((address < flashStart) || (address + size > flashEnd)) {
			return false;
		}
		return true;
	}



	bool IsLocked(void) {
		if (ReadBit(&flashHandle->CR, FLASH_CR_LOCK)) {
			return true;
		}
		return false;
	}



	bool IsBusy(void) {
		if (ReadBit(&flashHandle->SR, FLASH_SR_BSY)) {
			return true;
		}
		return false;
	}



	Status::statusType CheckEop(void) {
		if (!ReadBit(&flashHandle->SR, FLASH_SR_EOP)) {
			return Status::error;
		}

		ClearBit(&flashHandle->SR, FLASH_SR_EOP);

		return Status::ok;;
	}
};

