#pragma once
#include <BSP.h>


class SWD {
	static const int ACK_OK = 0x4;
	static const int ACK_WAIT = 0x2;
	static const int ACK_FAULT = 0x1;

private:
	uint32 tdiv2;

	int errors = 0;

	AGPIO& clockPin;
	AGPIO& dataPin;
	uint32 frequency;


public:
	SWD(AGPIO& clock, AGPIO& data, uint32 frequency):
		clockPin(clock), dataPin(data), frequency(frequency)
	{
		tdiv2 = 1000000 / (frequency / 2);
	}


	void Init() {
		clockPin.Reset().SetParameters({ AGPIO::Mode::Output });
		dataPin.Reset().SetParameters({ AGPIO::Mode::Output });

		Reset();
		Resync();
	}


	void Reset() {
		// TODO: Add SoftReset
	}


	// Sends 50 "1"s and 8 "0"s to resynchronize with the microcontroller.
	void Resync() {
		for (unsigned int i = 0; i < 50; i++) {
			WriteBit(1);
		}
		WriteBit(0);
		// The reference says that we have to read IDCODE here
		Read(false, 0);
	}


	uint32 Read(bool ap, int reg) {
		int ack = WriteCommand(ap, 1, reg);
		if (ack == ACK_OK) {
			uint32 value = 0;
			int parity = 0;
			for (unsigned int i = 0; i < 32; i++) {
				value >>= 1;
				int bit = ReadBit();
				value |= bit << 31;
				parity ^= bit;
			}
			if (parity == ReadBit()) {
				dataPin.SetParameters({ AGPIO::Mode::Output });
				WriteBit(0);
				errors = std::max(errors - 1, 0);
				return value;
			}
		} else if (ack == ACK_FAULT) {
			for (unsigned int i = 0; i < 33; i++) {
				Clock();
			}
			dataPin.SetParameters({ AGPIO::Mode::Output });
			WriteBit(0);
			Error();
			return Read(ap, reg);
		} else if (ack == ACK_WAIT) {
			/*
				for (unsigned int i = 0; i < 33; i++) {
					clock();
				}
			*/

			// Just try again until the command succeeds
			/*
				dataPin.SetParameters({ AGPIO::Mode::Output });
				return Read(ap, reg);
			*/
			errors -= 2;
		}
		Error();
		dataPin.SetParameters({ AGPIO::Mode::Output });
		Resync();
		return Read(ap, reg);
	}


	void Write(bool ap, int reg, uint32 value) {
		int parity = __builtin_parity(value);
		int ack = WriteCommand(ap, 0, reg);
		if (ack == ACK_OK) {
			Clock();
			dataPin.SetParameters({ AGPIO::Mode::Output });
			uint32 tmpValue = value;
			for (unsigned int i = 0; i < 32; i++) {
				WriteBit(tmpValue & 0x1);
				tmpValue >>= 1;
			}
			WriteBit(parity);
			WriteBit(0);
			errors = std::max(errors - 1, 0);
			return;
		} else if (ack == ACK_FAULT) {
			// TODO: ACK_FAULT
			//throw std::runtime_error("ACK_FAULT");
		} else if (ack == ACK_WAIT) {
			// Just try again until the command succeeds
			/*
				clock();
				dataPin.SetParameters({ AGPIO::Mode::Output });
				for (unsigned int i = 0; i < 32; i++) {
					WriteBit(0);
				}
				Write(ap, reg, value);
				return;
			*/
			errors -= 2;
		}
		Error();
		dataPin.SetParameters({ AGPIO::Mode::Output });
		Resync();
		Write(ap, reg, value);
	}


private:
	void WriteBit(bool value) {
		clockPin.Set();
		System::DelayUs(tdiv2);

		dataPin.SetState(value);

		clockPin.Reset();
		System::DelayUs(tdiv2);
	}


	uint8 ReadBit() {
		clockPin.Set();
		System::DelayUs(tdiv2);

		uint8 value = static_cast<uint8>(dataPin.GetState());

		clockPin.Reset();
		System::DelayUs(tdiv2);

		return value;
	}


	void Clock() {
		clockPin.Set();
		System::DelayUs(tdiv2);

		clockPin.Reset();
		System::DelayUs(tdiv2);
	}


	int WriteCommand(bool ap, int read, int reg) {
		WriteBit(1);
		WriteBit(ap ? 1 : 0);
		WriteBit(read);
		WriteBit(reg & 0x1);
		WriteBit((reg >> 1) & 0x1);
		int parity = (ap ? 1 : 0) + read + (reg & 0x1) + ((reg >> 1) & 0x1);
		WriteBit(parity & 0x1);
		WriteBit(0);
		dataPin.SetParameters({ AGPIO::Mode::Input });
		/*if (reg == 1) {
			uint32 data = 0;
			for (unsigned int i = 0; i < 32; i++) {
				data <<= 1;
				data |= ReadBit();
			}
			std::cout << std::hex << data << std::endl;
		}*/
		Clock();
		Clock();
		int ack = ReadBit() << 2;
		ack |= ReadBit() << 1;
		ack |= ReadBit();
		return ack;
	}


	void Error() {
		errors += 2;
		if (errors >= 10) {
			// TODO: runtime_error
			//throw std::runtime_error("Too many errors.");
		}
	}

};
