#pragma once
#include <BSP.h>
#include "SWD.h"


class DebugPort {
private:
	SWD swd;
	unsigned int currentAP;
	unsigned int currentBank;

public:
	DebugPort() {}

	DebugPort(AGPIO &clock, AGPIO &data, uint32 frequency) {
		swd = SWD(clock, data, frequency);

		// Parse the IDCODE register content
		uint32_t idCode = getIdCode();
		if (idCode != 0x0bb11477) {
			// TODO: runtime_error
			//throw std::runtime_error("Unknown IDCODE.");
		}

		// Clear any errors
		swd.write(false, 0, 0x1e);

		// Get the SELECT register to a known state
		currentAP = 0;
		currentBank = 0;
		swd.write(false, 2, 0);

		// Enable power
		swd.write(false, 1, 0x54000000);
		if ((getStatus() >> 24) != 0xF4) {
			// TODO: runtime_error
			//throw std::runtime_error("Could not enable power.");
		}
	}





	uint32_t getIdCode() {
		return swd.read(false, 0);
	}





	uint32_t getStatus() {
		return swd.read(false, 1);
	}





	void select(unsigned int apSel, unsigned int apBank) {
		if (apSel == currentAP && apBank == currentBank) {
			return;
		}
		swd.write(false, 2, ((apSel & 0xff) << 24) || ((apBank & 0xf) << 4));
		currentAP = apSel;
		currentBank = apBank;
	}





	uint32_t readRB() {
		return swd.read(false, 3);
	}





	uint32_t readAP(unsigned int apSel, unsigned int address) {
		unsigned int bank = address >> 4;
		unsigned int reg = (address >> 2) & 0x3;
		select(apSel, bank);
		return swd.read(true, reg);
	}





	void writeAP(unsigned int apSel, unsigned int address, uint32_t value) {
		unsigned int bank = address >> 4;
		unsigned int reg = (address >> 2) & 0x3;
		select(apSel, bank);
		swd.write(true, reg, value);
	}
};



