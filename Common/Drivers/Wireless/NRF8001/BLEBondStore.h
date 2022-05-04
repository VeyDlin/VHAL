#pragma once
#include <BSP.h>

class BLEBondStore {
public:
	// TODO: add flash mem Store

	BLEBondStore() { }

	BLEBondStore(int offset)
	//: _flashPageStartAddress((uint32_t *)(NRF_FICR->CODEPAGESIZE * (NRF_FICR->CODESIZE - 1 - (uint32_t)offset)))
			{
	}

	bool hasData() {
		//return (*this->_flashPageStartAddress != 0xFFFFFFFF);
		return false;
	}

	void clearData() {
		//while(sd_flash_page_erase(pageNo) == NRF_ERROR_BUSY);
	}

	void putData(const unsigned char *data, unsigned int offset, unsigned int length) {
		this->clearData();

		// while (sd_flash_write((uint32_t*)_flashPageStartAddress, (uint32_t*)data, (uint32_t)length/4) == NRF_ERROR_BUSY);
	}

	void getData(unsigned char *data, unsigned int offset, unsigned int length) {
		/* uint32_t *in = this->_flashPageStartAddress;
		 uint32_t *out  = (uint32_t*)data;

		 offset = offset;

		 for(unsigned int i = 0; i < length; i += 4) { // assumes length is multiple of 4
		 *out = *in;

		 out++;
		 in++;
		 }*/
	}

private:
	uint32_t *_flashPageStartAddress;
};

