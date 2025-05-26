#pragma once
#include <BSP.h>
#include "SWD.h"


class DebugPort : public SWD {
private:
	unsigned int currentAP;
	unsigned int currentBank;

public:
	DebugPort() {}

	DebugPort(AGPIO& clock, AGPIO& data, uint32 frequency) :
		SWD(clock, data, frequency)
	{

	}


	void Init() {
		SWD::Init();
		// Parse the IDCODE register content
		uint32 idCode = GetIdCode();
		if (idCode != 0x0bb11477) {
			// TODO: runtime_error
			//throw std::runtime_error("Unknown IDCODE.");
		}

		// Clear any errors
		Write(false, 0, 0x1e);

		// Get the SELECT register to a known state
		currentAP = 0;
		currentBank = 0;
		Write(false, 2, 0);

		// Enable power
		Write(false, 1, 0x54000000);
		if ((GetStatus() >> 24) != 0xF4) {
			// TODO: runtime_error
			//throw std::runtime_error("Could not enable power.");
		}
	}


	uint32 GetIdCode() {
		return Read(false, 0);
	}


	uint32 GetStatus() {
		return Read(false, 1);
	}


	void Select(unsigned int apSel, unsigned int apBank) {
		if (apSel == currentAP && apBank == currentBank) {
			return;
		}
		Write(false, 2, ((apSel & 0xff) << 24) || ((apBank & 0xf) << 4));
		currentAP = apSel;
		currentBank = apBank;
	}


	uint32 ReadRB() {
		return Read(false, 3);
	}


	uint32 ReadAP(unsigned int apSel, unsigned int address) {
		unsigned int bank = address >> 4;
		unsigned int reg = (address >> 2) & 0x3;
		Select(apSel, bank);
		return Read(true, reg);
	}


	void WriteAP(unsigned int apSel, unsigned int address, uint32 value) {
		unsigned int bank = address >> 4;
		unsigned int reg = (address >> 2) & 0x3;
		Select(apSel, bank);
		Write(true, reg, value);
	}
};
