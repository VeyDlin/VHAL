#pragma once
#include <System/System.h>


#define AUSED_FLASH_ADAPTER



class FLASHAdapter {
public:
	enum class FlashProtectionLevel {
	    Level0,     // No protection - full access (debug/programming allowed)
	    Level1,     // Partial protection - external access blocked, code runs
	    Level2,     // Maximum protection - permanent, cannot downgrade
	    Unknown     // Unrecognized protection level
	};

	struct Parameters {
		uint32 startAddress;
		uint32 endAddress;
	};


protected:
	FLASH_TypeDef *flashHandle;
	Parameters parameters;

public:
	FLASHAdapter() = default;
	FLASHAdapter(FLASH_TypeDef *flash) : flashHandle(flash) { }

	virtual Status::statusType SetParameters(const Parameters &params) {
		parameters = params;
		return Initialization();
	}

	virtual Status::statusType Unlock(uint32 key1, uint32 key2) = 0;
	virtual Status::statusType Lock() = 0;

	virtual Status::info<uint8> Read(uint8 *address) = 0;

	virtual Status::statusType WriteData(uint32 *address, const void *data, size_t size) = 0;
	virtual Status::statusType Write(uint16 *address, uint16 data) = 0;

	virtual Status::statusType PageErase(uint8 *address) = 0;
	virtual Status::statusType SectorErase(uint32 sectorNumber) = 0;
	virtual Status::statusType MassErase() = 0;

	virtual Status::statusType GetStatus() = 0;
	virtual Status::statusType ClearStatusFlags() = 0;

	virtual Status::info<uint32> ReadOptionBytes() = 0;
	virtual Status::statusType WriteOptionBytes(uint32 optionBytes) = 0;

	virtual bool IsReadProtected() const = 0;
	virtual uint8 GetReadProtectionLevel() const = 0;
	virtual FlashProtectionLevel GetProtectionLevel() const = 0;
	virtual Status::statusType SetReadProtectionLevel(FlashProtectionLevel level) = 0;
	virtual Status::statusType DisableReadProtection() = 0;

	virtual Status::statusType UnlockOptionBytes() = 0;
	virtual Status::statusType LockOptionBytes() = 0;
	virtual bool IsOptionBytesLocked() const = 0;


protected:
	virtual Status::statusType Initialization() = 0;
};
