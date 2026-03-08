#pragma once
#include "IAdapter.h"

#define VHAL_FLASH_ADAPTER


template<typename HandleType>
class FLASHAdapter : public IAdapter {
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
	HandleType *flashHandle;
	Parameters parameters;

public:
	FLASHAdapter() = default;
	FLASHAdapter(HandleType *flash) : flashHandle(flash) { }

	virtual ResultStatus SetParameters(const Parameters &params) {
		parameters = params;
		return Initialization();
	}

	virtual ResultStatus Unlock(uint32 key1, uint32 key2) = 0;
	virtual ResultStatus Lock() = 0;

	virtual Result<uint8> Read(uint8 *address) = 0;

	virtual ResultStatus WriteData(uint32 *address, const void *data, size_t size) = 0;
	virtual ResultStatus Write(uint16 *address, uint16 data) = 0;

	virtual ResultStatus PageErase(uint8 *address) = 0;
	virtual ResultStatus SectorErase(uint32 sectorNumber) = 0;
	virtual ResultStatus MassErase() = 0;

	virtual ResultStatus GetStatus() = 0;
	virtual ResultStatus ClearStatusFlags() = 0;

	virtual Result<uint32> ReadOptionBytes() = 0;
	virtual ResultStatus WriteOptionBytes(uint32 optionBytes) = 0;

	virtual bool IsReadProtected() const = 0;
	virtual uint8 GetReadProtectionLevel() const = 0;
	virtual FlashProtectionLevel GetProtectionLevel() const = 0;
	virtual ResultStatus SetReadProtectionLevel(FlashProtectionLevel level) = 0;
	virtual ResultStatus DisableReadProtection() = 0;

	virtual ResultStatus UnlockOptionBytes() = 0;
	virtual ResultStatus LockOptionBytes() = 0;
	virtual bool IsOptionBytesLocked() const = 0;


protected:
	virtual ResultStatus Initialization() = 0;
};
