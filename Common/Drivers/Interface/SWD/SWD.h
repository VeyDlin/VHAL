#pragma once
#include <BSP.h>


class SWD {
	static const int SWCLK = 12;
	static const int SWDAT = 13;
	static const int NRST = 14;


	static const int ACK_OK = 0x4;
	static const int ACK_WAIT = 0x2;
	static const int ACK_FAULT = 0x1;

private:
	uint32 tdiv2;

	int errors = 0;

	AGPIO *clockPin;
	AGPIO *dataPin;


public:
	SWD() {}

	SWD(AGPIO &clock, AGPIO &data, uint32 frequency) {
		/**
		 * SWD clock pin (with pulldown in the microcontroller).
		 */
		/**
		 * SWD data pin (with pullup in the microcontroller).
		 */

		clockPin = &clock;
		dataPin = &data;

		clockPin->SetParameters({ AGPIO::Mode::Output, AGPIO::Pull::Down, AGPIO::Speed::Medium });
		dataPin->SetParameters({ AGPIO::Mode::Output, AGPIO::Pull::Up, AGPIO::Speed::Medium });

		clockPin->Reset();
		dataPin->Reset();

		SoftReset();
		resync();
	}





	void SoftReset() {
		// TODO: SoftReset
		//pinMode(NRST, OUTPUT);
		//digitalWrite(NRST, 0);
		//System::DelayMs(200);
		//pinMode(NRST, INPUT);
	}





	// Sends 50 "1"s and 8 "0"s to resynchronize with the microcontroller.
	void resync() {
		for (unsigned int i = 0; i < 50; i++) {
			writeBit(1);
		}
		writeBit(0);
		// The reference says that we have to read IDCODE here
		read(false, 0);
	}





	uint32_t read(bool ap, int reg) {
		int ack = writeCommand(ap, 1, reg);
		if (ack == ACK_OK) {
			uint32_t value = 0;
			int parity = 0;
			for (unsigned int i = 0; i < 32; i++) {
				value >>= 1;
				int bit = readBit();
				value |= bit << 31;
				parity ^= bit;
			}
			if (parity == readBit()) {
				// TODO: ----------------------- pinMode(SWDAT, OUTPUT);
				writeBit(0);
				errors = std::max(errors - 1, 0);
				return value;
			}
		} else if (ack == ACK_FAULT) {
			for (unsigned int i = 0; i < 33; i++) {
				clock();
			}
			// TODO: ----------------------- pinMode(SWDAT, OUTPUT);
			writeBit(0);
			error();
			return read(ap, reg);
		} else if (ack == ACK_WAIT) {
			/*
			for (unsigned int i = 0; i < 33; i++) {
				clock();
			}
			*/

			// Just try again until the command succeeds
			/*
			pinMode(SWDAT, OUTPUT);
			return read(ap, reg);
			*/
			errors -= 2;
		}
		error();
		// TODO: ----------------------- pinMode(SWDAT, OUTPUT);
		resync();
		return read(ap, reg);
	}





	void write(bool ap, int reg, uint32_t value) {
		int parity = __builtin_parity(value);
		int ack = writeCommand(ap, 0, reg);
		if (ack == ACK_OK) {
			clock();
			// TODO: ----------------------- pinMode(SWDAT, OUTPUT);
			uint32_t tmpValue = value;
			for (unsigned int i = 0; i < 32; i++) {
				writeBit(tmpValue & 0x1);
				tmpValue >>= 1;
			}
			writeBit(parity);
			writeBit(0);
			errors = std::max(errors - 1, 0);
			return;
		} else if (ack == ACK_FAULT) {
			// TODO: ACK_FAULT
			//throw std::runtime_error("ACK_FAULT");
		} else if (ack == ACK_WAIT) {
		// Just try again until the command succeeds
		/*
		 	clock();
			pinMode(SWDAT, OUTPUT);
			for (unsigned int i = 0; i < 32; i++) {
				writeBit(0);
			}
			write(ap, reg, value);
			return;
		*/
			errors -= 2;
		}
		error();
		// TODO: ----------------------- pinMode(SWDAT, OUTPUT);
		resync();
		write(ap, reg, value);
	}


private:
	void writeBit(bool value) {
		clockPin->Set();
		System::DelayUs(tdiv2);

		dataPin->SetState(value);

		clockPin->Reset();
		System::DelayUs(tdiv2);
	}


	uint8 readBit() {
		clockPin->Set();
		System::DelayUs(tdiv2);

		uint8 value = static_cast<uint8>(dataPin->GetState());

		clockPin->Reset();
		System::DelayUs(tdiv2);

		return value;
	}


	void clock() {
		clockPin->Set();
		System::DelayUs(tdiv2);

		clockPin->Reset();
		System::DelayUs(tdiv2);
	}





	int writeCommand(bool ap, int read, int reg) {
		writeBit(1);
		writeBit(ap ? 1 : 0);
		writeBit(read);
		writeBit(reg & 0x1);
		writeBit((reg >> 1) & 0x1);
		int parity = (ap ? 1 : 0) + read + (reg & 0x1) + ((reg >> 1) & 0x1);
		writeBit(parity & 0x1);
		writeBit(0);
		// TODO: ----------------------- pinMode(SWDAT, INPUT);
		/*if (reg == 1) {
			uint32_t data = 0;
			for (unsigned int i = 0; i < 32; i++) {
				data <<= 1;
				data |= readBit();
			}
			std::cout << std::hex << data << std::endl;
		}*/
		clock();
		clock();
		int ack = readBit() << 2;
		ack |= readBit() << 1;
		ack |= readBit();
		return ack;
	}





	void error() {
		errors += 2;
		if (errors >= 10) {
			// TODO: runtime_error
			//throw std::runtime_error("Too many errors.");
		}
	}

};


