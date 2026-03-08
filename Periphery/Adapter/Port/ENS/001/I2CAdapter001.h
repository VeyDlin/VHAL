#pragma once
#include <Adapter/I2CAdapter.h>


using AI2C = class I2CAdapter001;

class I2CAdapter001 : public I2CAdapter<CMSDK_I2C_TypeDef> {
public:
	I2CAdapter001() { }
	I2CAdapter001(CMSDK_I2C_TypeDef *i2c, uint32 busClockHz):I2CAdapter(i2c, busClockHz) { }



	virtual void IrqEventHandler() override {
		uint32 sts = i2cHandle->I2C_STS;

		if (sts & (1 << 8)) {  // BUS_ERR
			i2cHandle->I2C_STS_CLR = (1 << 8);
			CallError(Error::MisplacedStartStop);
			return;
		}

		if (sts & (1 << 9)) {  // ARB_LOST
			i2cHandle->I2C_STS_CLR = (1 << 9);
			CallError(Error::ArbitrationLost);
			return;
		}

		if (sts & (1 << 10)) { // ACK_FAIL
			i2cHandle->I2C_STS_CLR = (1 << 10);
			CallError(Error::AcknowledgeFailure);
			return;
		}

		if (sts & (1 << 11)) { // OVR
			i2cHandle->I2C_STS_CLR = (1 << 11);
			CallError(Error::OverUnderRun);
			return;
		}

		if (parameters.mode == Mode::Master) {
			MasterIrqEvent(sts);
		} else {
			SlaveIrqEvent(sts);
		}
	}



	virtual void IrqErrorHandler() override {
		uint32 sts = i2cHandle->I2C_STS;

		if (sts & (1 << 8)) {  // BUS_ERR
			i2cHandle->I2C_STS_CLR = (1 << 8);
			CallError(Error::MisplacedStartStop);
		}

		if (sts & (1 << 9)) {  // ARB_LOST
			i2cHandle->I2C_STS_CLR = (1 << 9);
			CallError(Error::ArbitrationLost);
		}

		if (sts & (1 << 10)) { // ACK_FAIL
			i2cHandle->I2C_STS_CLR = (1 << 10);
			CallError(Error::AcknowledgeFailure);
		}

		if (sts & (1 << 11)) { // OVR
			i2cHandle->I2C_STS_CLR = (1 << 11);
			CallError(Error::OverUnderRun);
		}

		// Disable interrupts and generate STOP
		i2cHandle->I2C_CR2 &= ~((1 << 7) | (1 << 6) | (1 << 8));
		i2cHandle->I2C_CR1 |= (1 << 4); // STOP
		state = ResultStatus::error;
	}



public:
	virtual ResultStatus CheckDevice(uint8 deviceAddress, uint16 repeat) override {
		return ResultStatus::notSupported;
	}


	virtual ResultStatus CheckDeviceAsync(uint8 deviceAddress, uint16 repeat) override {
		return ResultStatus::notSupported;
	}


	virtual Result<uint8> Scan(uint8 *listBuffer, uint8 size) override {
		return ResultStatus::notSupported;
	}


	virtual Result<uint8> ScanAsync(uint8 *listBuffer, uint8 size) override {
		return ResultStatus::notSupported;
	}



private:
	enum class AsyncDirection { Transmit, Receive };
	enum class AsyncPhase { Start, AddressWrite, RegAddr, Data, Restart, AddressRead, DataRead };

	AsyncDirection asyncDirection = AsyncDirection::Transmit;
	AsyncPhase asyncPhase = AsyncPhase::Start;
	uint8 asyncRegAddrBytesSent = 0;


	inline void AsyncComplete() {
		// Disable all I2C interrupts
		i2cHandle->I2C_CR2 &= ~((1 << 7) | (1 << 6) | (1 << 8));
		state = ResultStatus::ready;

		if (onComplete) {
			onComplete();
		}
	}


	inline void AsyncError() {
		i2cHandle->I2C_CR1 |= (1 << 4); // STOP
		// Disable all I2C interrupts
		i2cHandle->I2C_CR2 &= ~((1 << 7) | (1 << 6) | (1 << 8));
		state = ResultStatus::error;
	}


	inline void SlaveIrqEvent(uint32 sts) {
		// Slave mode not implemented for ENS001
	}


	inline void MasterIrqEvent(uint32 sts) {
		// START bit sent
		if (sts & (1 << 0)) {
			if (asyncPhase == AsyncPhase::Start || asyncPhase == AsyncPhase::AddressWrite || asyncPhase == AsyncPhase::RegAddr) {
				// Send device address + Write
				i2cHandle->I2C_DR = (deviceAddress << 1) & ~1U;
				asyncPhase = AsyncPhase::AddressWrite;
			} else if (asyncPhase == AsyncPhase::Restart || asyncPhase == AsyncPhase::AddressRead) {
				// Send device address + Read
				i2cHandle->I2C_DR = (deviceAddress << 1) | 1U;
				asyncPhase = AsyncPhase::AddressRead;
			}
			return;
		}

		// ADDR cleared (address sent + ACK received)
		if (sts & (1 << 1)) {
			(void)i2cHandle->I2C_STS; // Clear ADDR

			if (asyncPhase == AsyncPhase::AddressWrite) {
				asyncPhase = AsyncPhase::RegAddr;
				asyncRegAddrBytesSent = 0;
			} else if (asyncPhase == AsyncPhase::AddressRead) {
				asyncPhase = AsyncPhase::DataRead;
				if (rxDataNeed == 1) {
					i2cHandle->I2C_CR1 &= ~(1 << 5); // Clear ACK
					i2cHandle->I2C_CR1 |= (1 << 4);   // STOP
				}
			}
			return;
		}

		// TXE - transmit buffer empty
		if (sts & (1 << 6)) {
			if (asyncPhase == AsyncPhase::RegAddr) {
				// Send register address bytes
				if (asyncRegAddrBytesSent < registerAddressSize) {
					if (registerAddressSize == 2 && asyncRegAddrBytesSent == 0) {
						i2cHandle->I2C_DR = (registerAddress >> 8) & 0xFF;
					} else {
						i2cHandle->I2C_DR = registerAddress & 0xFF;
					}
					asyncRegAddrBytesSent++;
				} else {
					if (asyncDirection == AsyncDirection::Transmit) {
						asyncPhase = AsyncPhase::Data;
						// Send first data byte
						if (txDataCounter < txDataNeed) {
							i2cHandle->I2C_DR = *txDataPointer++;
							txDataCounter++;
						}
					} else {
						// For read: generate RESTART
						asyncPhase = AsyncPhase::Restart;
						i2cHandle->I2C_CR1 |= (1 << 3); // START (repeated)
					}
				}
			} else if (asyncPhase == AsyncPhase::Data) {
				if (txDataCounter < txDataNeed) {
					i2cHandle->I2C_DR = *txDataPointer++;
					txDataCounter++;
				}
			}
			return;
		}

		// BTF - byte transfer finished
		if (sts & (1 << 2)) {
			if (asyncPhase == AsyncPhase::Data && txDataCounter >= txDataNeed) {
				// All data sent, generate STOP
				i2cHandle->I2C_CR1 |= (1 << 4); // STOP
				AsyncComplete();
			}
			return;
		}

		// RXNE - receive buffer not empty
		if (sts & (1 << 5)) {
			if (asyncPhase == AsyncPhase::DataRead) {
				*rxDataPointer++ = (uint8)i2cHandle->I2C_DR;
				rxDataCounter++;

				if (rxDataCounter >= rxDataNeed) {
					AsyncComplete();
				} else if (rxDataCounter == rxDataNeed - 1) {
					// NACK + STOP for last byte
					i2cHandle->I2C_CR1 &= ~(1 << 5); // Clear ACK
					i2cHandle->I2C_CR1 |= (1 << 4);   // STOP
				}
			}
			return;
		}
	}



	// Wait for a specific STS bit to be set, with timeout
	inline ResultStatus WaitFlag(uint32 flag) {
		uint32 tickStart = System::GetTick();
		while (!(i2cHandle->I2C_STS & flag)) {
			if ((System::GetTick() - tickStart) > timeout) {
				return ResultStatus::timeout;
			}
		}
		return ResultStatus::ok;
	}


	// Wait for a specific STS bit to be cleared, with timeout
	inline ResultStatus WaitFlagClear(uint32 flag) {
		uint32 tickStart = System::GetTick();
		while (i2cHandle->I2C_STS & flag) {
			if ((System::GetTick() - tickStart) > timeout) {
				return ResultStatus::timeout;
			}
		}
		return ResultStatus::ok;
	}


	// Check for error flags in STS register
	inline ResultStatus CheckErrors() {
		uint32 sts = i2cHandle->I2C_STS;

		if (sts & (1 << 10)) { // ACK_FAIL
			i2cHandle->I2C_STS_CLR = (1 << 10);
			i2cHandle->I2C_CR1 |= (1 << 4); // STOP
			return ResultStatus::error;
		}

		if (sts & (1 << 8)) { // BUS_ERR
			i2cHandle->I2C_STS_CLR = (1 << 8);
			return ResultStatus::error;
		}

		if (sts & (1 << 9)) { // ARB_LOST
			i2cHandle->I2C_STS_CLR = (1 << 9);
			return ResultStatus::error;
		}

		return ResultStatus::ok;
	}


	// Wait for TXE flag with error checking
	inline ResultStatus WaitTXE() {
		uint32 tickStart = System::GetTick();
		while (!(i2cHandle->I2C_STS & (1 << 6))) { // TXE
			if (CheckErrors() != ResultStatus::ok) {
				return ResultStatus::error;
			}
			if ((System::GetTick() - tickStart) > timeout) {
				return ResultStatus::timeout;
			}
		}
		return ResultStatus::ok;
	}


	// Wait for RXNE flag with error checking
	inline ResultStatus WaitRXNE() {
		uint32 tickStart = System::GetTick();
		while (!(i2cHandle->I2C_STS & (1 << 5))) { // RXNE
			if (CheckErrors() != ResultStatus::ok) {
				return ResultStatus::error;
			}
			if ((System::GetTick() - tickStart) > timeout) {
				return ResultStatus::timeout;
			}
		}
		return ResultStatus::ok;
	}


	// Wait for BTF (Byte Transfer Finished) flag
	inline ResultStatus WaitBTF() {
		uint32 tickStart = System::GetTick();
		while (!(i2cHandle->I2C_STS & (1 << 2))) { // BTF
			if (CheckErrors() != ResultStatus::ok) {
				return ResultStatus::error;
			}
			if ((System::GetTick() - tickStart) > timeout) {
				return ResultStatus::timeout;
			}
		}
		return ResultStatus::ok;
	}


	// Wait for bus not busy
	inline ResultStatus WaitBusNotBusy() {
		uint32 tickStart = System::GetTick();
		while (i2cHandle->I2C_STS & (1 << 13)) { // BUSY
			if ((System::GetTick() - tickStart) > timeout) {
				return ResultStatus::timeout;
			}
		}
		return ResultStatus::ok;
	}



private:
	inline void OnEnableClock() {
#ifdef CMSDK_I2C0
		if (i2cHandle == CMSDK_I2C0) {
			CMSDK_SYSCON->APB_CLKEN |= (1 << 4);
			return;
		}
#endif
#ifdef CMSDK_I2C1
		if (i2cHandle == CMSDK_I2C1) {
			CMSDK_SYSCON->APB_CLKEN |= (1 << 5);
			return;
		}
#endif
	}


protected:
	virtual ResultStatus Initialization() override {
		OnEnableClock();

		auto status = BeforeInitialization();
		if (status != ResultStatus::ok) {
			return status;
		}

		// Software reset
		i2cHandle->I2C_CR1 |= (1 << 15);  // SWRST
		i2cHandle->I2C_CR1 &= ~(1 << 15); // Release SWRST

		// Set speed selection in CR2 (ENS001 uses predefined speed constants)
		uint32 speedSel = 0x9; // Default: 100 kHz
		switch (parameters.speed) {
			case Speed::Low:      speedSel = 0x0;  break; // 10 kHz
			case Speed::Standard: speedSel = 0x9;  break; // 100 kHz
			case Speed::Fast:     speedSel = 0x27; break; // 400 kHz
			case Speed::FastPlus: speedSel = 0x2E; break; // 1500 kHz (closest high mode)
		}
		i2cHandle->I2C_CR2 = (i2cHandle->I2C_CR2 & ~0x3F) | (speedSel & 0x3F);

		// Configure clock stretching
		if (!parameters.clockStretching) {
			i2cHandle->I2C_CR1 |= (1 << 2); // NOSTRETCH
		} else {
			i2cHandle->I2C_CR1 &= ~(1 << 2);
		}

		// Set own address for slave mode
		i2cHandle->I2C_OAR = parameters.slaveAddress;

		// Enable ACK
		i2cHandle->I2C_CR1 |= (1 << 5); // ACK

		// Enable peripheral
		i2cHandle->I2C_CR1 |= (1 << 0); // PE

		return AfterInitialization();
	}



public:
	virtual ResultStatus WriteByteArray(uint8 device, uint16 address, uint8 addressSize, uint8 *writeData, uint32 dataSize) override {
		// Wait for bus not busy
		if (WaitBusNotBusy() != ResultStatus::ok) {
			return ResultStatus::busy;
		}

		// Ensure peripheral is enabled
		if (!(i2cHandle->I2C_CR1 & (1 << 0))) {
			i2cHandle->I2C_CR1 |= (1 << 0); // PE
		}

		// Enable ACK
		i2cHandle->I2C_CR1 |= (1 << 5); // ACK

		// Generate START
		i2cHandle->I2C_CR1 |= (1 << 3); // START
		if (WaitFlag(1 << 0) != ResultStatus::ok) { // STARTBIT
			return ResultStatus::error;
		}

		// Send device address + Write (bit 0 = 0)
		i2cHandle->I2C_DR = (device << 1) & ~1U;
		if (WaitFlag(1 << 1) != ResultStatus::ok) { // ADDR
			i2cHandle->I2C_CR1 |= (1 << 4); // STOP
			return ResultStatus::error;
		}
		// Clear ADDR by reading STS
		(void)i2cHandle->I2C_STS;

		// Send register address bytes
		if (addressSize >= 2) {
			if (WaitTXE() != ResultStatus::ok) {
				i2cHandle->I2C_CR1 |= (1 << 4); // STOP
				return ResultStatus::error;
			}
			i2cHandle->I2C_DR = (address >> 8) & 0xFF;
		}
		if (addressSize >= 1) {
			if (WaitTXE() != ResultStatus::ok) {
				i2cHandle->I2C_CR1 |= (1 << 4); // STOP
				return ResultStatus::error;
			}
			i2cHandle->I2C_DR = address & 0xFF;
		}

		// Send data bytes
		while (dataSize > 0) {
			if (WaitTXE() != ResultStatus::ok) {
				i2cHandle->I2C_CR1 |= (1 << 4); // STOP
				return ResultStatus::error;
			}
			i2cHandle->I2C_DR = *writeData;
			writeData++;
			dataSize--;

			// If BTF is set and more data, write next byte immediately
			if ((i2cHandle->I2C_STS & (1 << 2)) && (dataSize != 0)) {
				i2cHandle->I2C_DR = *writeData;
				writeData++;
				dataSize--;
			}
		}

		// Wait for BTF before STOP
		if (WaitBTF() != ResultStatus::ok) {
			i2cHandle->I2C_CR1 |= (1 << 4); // STOP
			return ResultStatus::error;
		}

		// Generate STOP
		i2cHandle->I2C_CR1 |= (1 << 4); // STOP

		return ResultStatus::ok;
	}



	virtual ResultStatus ReadByteArray(uint8 device, uint16 address, uint8 addressSize, uint8 *readData, uint32 dataSize) override {
		// Wait for bus not busy
		if (WaitBusNotBusy() != ResultStatus::ok) {
			return ResultStatus::busy;
		}

		// Ensure peripheral is enabled
		if (!(i2cHandle->I2C_CR1 & (1 << 0))) {
			i2cHandle->I2C_CR1 |= (1 << 0); // PE
		}

		// Enable ACK
		i2cHandle->I2C_CR1 |= (1 << 5); // ACK

		// Generate START
		i2cHandle->I2C_CR1 |= (1 << 3); // START
		if (WaitFlag(1 << 0) != ResultStatus::ok) { // STARTBIT
			return ResultStatus::error;
		}

		// Send device address + Write (for register address phase)
		i2cHandle->I2C_DR = (device << 1) & ~1U;
		if (WaitFlag(1 << 1) != ResultStatus::ok) { // ADDR
			i2cHandle->I2C_CR1 |= (1 << 4); // STOP
			return ResultStatus::error;
		}
		// Clear ADDR
		(void)i2cHandle->I2C_STS;

		// Send register address bytes
		if (addressSize >= 2) {
			if (WaitTXE() != ResultStatus::ok) {
				i2cHandle->I2C_CR1 |= (1 << 4); // STOP
				return ResultStatus::error;
			}
			i2cHandle->I2C_DR = (address >> 8) & 0xFF;
		}
		if (addressSize >= 1) {
			if (WaitTXE() != ResultStatus::ok) {
				i2cHandle->I2C_CR1 |= (1 << 4); // STOP
				return ResultStatus::error;
			}
			i2cHandle->I2C_DR = address & 0xFF;
		}

		// Wait for BTF before RESTART (byte must finish transmitting)
		if (WaitBTF() != ResultStatus::ok) {
			i2cHandle->I2C_CR1 |= (1 << 4); // STOP
			return ResultStatus::error;
		}

		// Generate RESTART
		i2cHandle->I2C_CR1 |= (1 << 3); // START (repeated)
		if (WaitFlag(1 << 0) != ResultStatus::ok) { // STARTBIT
			return ResultStatus::error;
		}

		// Send device address + Read (bit 0 = 1)
		i2cHandle->I2C_DR = (device << 1) | 1U;
		if (WaitFlag(1 << 1) != ResultStatus::ok) { // ADDR
			i2cHandle->I2C_CR1 |= (1 << 4); // STOP
			return ResultStatus::error;
		}

		if (dataSize == 0) {
			// Clear ADDR, then STOP
			(void)i2cHandle->I2C_STS;
			i2cHandle->I2C_CR1 |= (1 << 4); // STOP
		} else if (dataSize == 1) {
			// NACK before clearing ADDR
			i2cHandle->I2C_CR1 &= ~(1 << 5); // Clear ACK
			(void)i2cHandle->I2C_STS;         // Clear ADDR
			i2cHandle->I2C_CR1 |= (1 << 4);   // STOP
		} else if (dataSize == 2) {
			// NACK, set POS-like behavior
			i2cHandle->I2C_CR1 &= ~(1 << 5); // Clear ACK
			(void)i2cHandle->I2C_STS;         // Clear ADDR
		} else {
			// ACK is already enabled
			(void)i2cHandle->I2C_STS; // Clear ADDR
		}

		// Read data bytes
		uint32 count = dataSize;
		while (count > 0) {
			if (count <= 3) {
				if (count == 1) {
					if (WaitRXNE() != ResultStatus::ok) {
						return ResultStatus::error;
					}
					*readData = (uint8)i2cHandle->I2C_DR;
					readData++;
					count--;
				} else if (count == 2) {
					if (WaitBTF() != ResultStatus::ok) {
						return ResultStatus::error;
					}
					i2cHandle->I2C_CR1 |= (1 << 4); // STOP
					*readData = (uint8)i2cHandle->I2C_DR;
					readData++;
					count--;
					*readData = (uint8)i2cHandle->I2C_DR;
					readData++;
					count--;
				} else { // count == 3
					if (WaitBTF() != ResultStatus::ok) {
						return ResultStatus::error;
					}
					i2cHandle->I2C_CR1 &= ~(1 << 5); // Clear ACK
					*readData = (uint8)i2cHandle->I2C_DR;
					readData++;
					count--;
					if (WaitBTF() != ResultStatus::ok) {
						return ResultStatus::error;
					}
					i2cHandle->I2C_CR1 |= (1 << 4); // STOP
					*readData = (uint8)i2cHandle->I2C_DR;
					readData++;
					count--;
					*readData = (uint8)i2cHandle->I2C_DR;
					readData++;
					count--;
				}
			} else {
				if (WaitRXNE() != ResultStatus::ok) {
					return ResultStatus::error;
				}
				*readData = (uint8)i2cHandle->I2C_DR;
				readData++;
				count--;

				// Double-read if BTF is set (guard: must not skip count==3 NACK path)
				if (count > 3 && (i2cHandle->I2C_STS & (1 << 2))) {
					*readData = (uint8)i2cHandle->I2C_DR;
					readData++;
					count--;
				}
			}
		}

		return ResultStatus::ok;
	}



	virtual ResultStatus WriteByteArrayAsync(uint8 device, uint16 address, uint8 addressSize, uint8 *writeData, uint32 dataSize) override {
		if (state != ResultStatus::ready) {
			return ResultStatus::busy;
		}

		state = ResultStatus::busy;
		asyncDirection = AsyncDirection::Transmit;
		asyncPhase = AsyncPhase::Start;

		deviceAddress = device;
		registerAddress = address;
		registerAddressSize = addressSize;
		txDataPointer = writeData;
		txDataNeed = dataSize;
		txDataCounter = 0;
		asyncRegAddrBytesSent = 0;

		// Enable event and error interrupts
		i2cHandle->I2C_CR2 |= (1 << 7) | (1 << 6); // ITEVTEN | ITERREN
		i2cHandle->I2C_CR2 |= (1 << 8); // ITBUFEN

		// Generate START
		i2cHandle->I2C_CR1 |= (1 << 5); // ACK
		i2cHandle->I2C_CR1 |= (1 << 3); // START

		return ResultStatus::ok;
	}


	virtual ResultStatus ReadByteArrayAsync(uint8 device, uint16 address, uint8 addressSize, uint8 *readData, uint32 dataSize) override {
		if (state != ResultStatus::ready) {
			return ResultStatus::busy;
		}

		state = ResultStatus::busy;
		asyncDirection = AsyncDirection::Receive;
		asyncPhase = AsyncPhase::Start;

		deviceAddress = device;
		registerAddress = address;
		registerAddressSize = addressSize;
		rxDataPointer = readData;
		rxDataNeed = dataSize;
		rxDataCounter = 0;
		asyncRegAddrBytesSent = 0;

		// Enable event and error interrupts
		i2cHandle->I2C_CR2 |= (1 << 7) | (1 << 6); // ITEVTEN | ITERREN
		i2cHandle->I2C_CR2 |= (1 << 8); // ITBUFEN

		// Generate START
		i2cHandle->I2C_CR1 |= (1 << 5); // ACK
		i2cHandle->I2C_CR1 |= (1 << 3); // START

		return ResultStatus::ok;
	}

};
