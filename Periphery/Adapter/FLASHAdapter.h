#pragma once
#include <Periphery.h>
#include <Status.h>

#define AUSED_FLASH_ADAPTER



class FLASHAdapter {
protected:
	FLASH_TypeDef *flashHandle;

	uint32 satartAddress;
	uint32 endAddress;

public:
	FLASHAdapter() { }
	FLASHAdapter(FLASH_TypeDef *flash, uint32 start, uint32 end): flashHandle(flash), satartAddress(start), endAddress(end)  { }

	virtual void Unlock(uint32 kye1, uint32 key2) = 0;
	virtual Status::info<uint8> Read(uint8 *address);
	virtual Status::statusType Write(uint16 *address, uint16 data);
	virtual Status::statusType PageErase(uint8 *address);
};
