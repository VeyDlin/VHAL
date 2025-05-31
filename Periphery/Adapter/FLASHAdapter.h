#pragma once
#include <Periphery.h>
#include <Status.h>

#define AUSED_FLASH_ADAPTER


class FLASHAdapter {
public:
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

	virtual Status::statusType ReadOptionBytes() = 0;
	virtual Status::statusType WriteOptionBytes() = 0;


protected:
	virtual Status::statusType Initialization() = 0;
};
