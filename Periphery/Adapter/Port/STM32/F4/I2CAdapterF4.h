#pragma once
#include <Utilities/WaitCondition/WaitCondition.h>
#include <Adapter/I2CAdapter.h>

using namespace WaitCondition;


// I2C master on the F4 peripheral. Blocking calls poll the flags with the adapter timeout. Asynchronous calls run on the
// event and error interrupts: the event handler walks the transfer through the phases below, one bus event at a time.
//
// The BSP must do the following before the first SetParameters():
//   - Enable the bus clock (RCC APB1) and the GPIO alternate function of SCL and SDA, open drain.
//   - Enable NVIC I2Cx_EV_IRQn and I2Cx_ER_IRQn, and call IrqHandler() from both vectors. IrqHandler() picks the error
//     path when an error flag is raised with its interrupt enabled, and the event path otherwise.
//
// Asynchronous transfers with a register address (addressSize 1 or 2) are write-then-read sequences: the register address
// is sent after the device address, and a read continues with a repeated START. The register address is sent high byte
// first. The addressSize of an asynchronous read may be 0, in which case the read starts at once.
//
// Not supported: 10-bit addressing, slave mode (SlaveIrqEvent does nothing yet), FastPlus (1 MHz), asynchronous CheckDevice
// and ScanAsync, and continuous reception.


using AI2C = class I2CAdapterF4;


class I2CAdapterF4 : public I2CAdapter<I2C_TypeDef> {
private:
	// Where the master is in a transfer. The event interrupt moves it on from one bus event to the next.
	enum class MasterPhase {
		WriteAddress,	// START sent. The device address for write goes out on SB.
		Transmit,		// Device address for write acknowledged. Register address and data bytes go out.
		ReadAddress,	// Repeated START sent. The device address for read goes out on SB.
		Receive			// Device address for read acknowledged. Data bytes come in.
	};


	MasterPhase masterPhase = MasterPhase::WriteAddress;
	bool isReadTransfer = false;
	uint8 registerBytesSent = 0;



public:
	I2CAdapterF4() = default;
	I2CAdapterF4(I2C_TypeDef *i2c, uint32 busClockHz):I2CAdapter(i2c, busClockHz) { }



	virtual void IrqEventHandler() override {
		if(parameters.mode ==  Mode::Master) {
			MasterIrqEvent();
		} else {
			SlaveIrqEvent();
		}
	}



	virtual void IrqErrorHandler() override {
		if (!LL_I2C_IsEnabledIT_ERR(i2cHandle)) {
			return;
		}

		Error found[4] = { Error::None, Error::None, Error::None, Error::None };
		uint8 count = 0;

		if (LL_I2C_IsActiveFlag_BERR(i2cHandle)) {
			LL_I2C_ClearFlag_BERR(i2cHandle);
			found[count++] = Error::MisplacedStartStop;
		}

		if (LL_I2C_IsActiveFlag_ARLO(i2cHandle)) {
			LL_I2C_ClearFlag_ARLO(i2cHandle);
			found[count++] = Error::ArbitrationLost;
		}

		if (LL_I2C_IsActiveFlag_AF(i2cHandle)) {
			LL_I2C_ClearFlag_AF(i2cHandle);
			found[count++] = Error::AcknowledgeFailure;
		}

		if (LL_I2C_IsActiveFlag_OVR(i2cHandle)) {
			LL_I2C_ClearFlag_OVR(i2cHandle);
			found[count++] = Error::OverUnderRun;
		}

		if (count == 0) {
			return;
		}

		// A failed address or data byte, an overrun, or a misplaced START or STOP leaves the bus held until the master sends
		// STOP. Arbitration loss releases the bus by itself, so it does not need STOP.
		bool stopNeeded = false;
		for (uint8 i = 0; i < count; i++) {
			if (found[i] != Error::ArbitrationLost) {
				stopNeeded = true;
			}
		}

		if (stopNeeded && state == ResultStatus::busy) {
			LL_I2C_GenerateStopCondition(i2cHandle);
		}

		EndTransfer(ResultStatus::error);

		for (uint8 i = 0; i < count; i++) {
			CallError(found[i]);
		}
	}



	// Dispatches the vector of an error or an event. Error flags are served only when their interrupt is enabled. Otherwise
	// the flags are left alone, and an event is handled.
	inline void IrqHandler() {
		bool errorRaised = LL_I2C_IsActiveFlag_BERR(i2cHandle) || LL_I2C_IsActiveFlag_ARLO(i2cHandle) ||
			LL_I2C_IsActiveFlag_AF(i2cHandle) || LL_I2C_IsActiveFlag_OVR(i2cHandle);

		if (errorRaised && LL_I2C_IsEnabledIT_ERR(i2cHandle)) {
			IrqErrorHandler();
		} else {
			IrqEventHandler();
		}
	}



	// Waits for the end of the asynchronous transfer. The handlers write the state from interrupt context, so every pass
	// reads it through a volatile access. A plain read could be hoisted out of the loop, and the wait would never end.
	virtual ResultStatus Await() override {
		ResultStatus current = ReadState();

		while (current != ResultStatus::ready && current != ResultStatus::error) {
			current = ReadState();
		}

		return current == ResultStatus::ready ? ResultStatus::ok : ResultStatus::error;
	}



	// The clock and the pins are set up only while the bus is idle, so a transfer keeps the settings it started with.
	virtual ResultStatus SetParameters(Parameters val) override {
		if (state == ResultStatus::busy) {
			return ResultStatus::busy;
		}

		parameters = val;
		return Initialization();
	}



	virtual ResultStatus StartSlaveListen() {
		return ResultStatus::notSupported;
	}


	virtual ResultStatus StopSlaveListen() {
		return ResultStatus::notSupported;
	}



	virtual ResultStatus CheckDevice(uint8 device, uint16 repeat) override {
		uint16 repeatCount = 0;
		uint64 tickStart = System::GetTick();

		// Wait for the BUSY flag to clear
		if (isBusyFlag(SET) != ResultStatus::ok) {
			return ResultStatus::busy;
		}

		// Enable the peripheral
		if ((i2cHandle->CR1 & I2C_CR1_PE) != I2C_CR1_PE) {
			i2cHandle->CR1 |= I2C_CR1_PE;
		}

		// Disable POS
		i2cHandle->CR1 &= ~I2C_CR1_POS;

		do {
			// Start
			i2cHandle->CR1 |= I2C_CR1_START;

			tickStart = System::GetTick();
			if (isSB_Flag(RESET) != ResultStatus::ok) {
				return ResultStatus::error;
			}

			// Send the address with the RD bit cleared
			i2cHandle->DR = static_cast<uint8>((device << 1) & ~1U);

			tickStart = System::GetTick();
			bool addressFlag = (i2cHandle->SR1 & I2C_SR1_ADDR) != 0;
			bool acknowledgeFailureFlag = (i2cHandle->SR1 & I2C_SR1_AF) != 0;

			// No flags yet, keep waiting
			while (!addressFlag && !acknowledgeFailureFlag) {
				if (System::GetTick() > (tickStart + timeout)) {
					break;
				}
				addressFlag = (i2cHandle->SR1 & I2C_SR1_ADDR) != 0;
				acknowledgeFailureFlag = (i2cHandle->SR1 & I2C_SR1_AF) != 0;
			}

			// The ADDR flag is set: the device acknowledged
			if (i2cHandle->SR1 & I2C_SR1_ADDR) {
				// Send STOP
				i2cHandle->CR1 |= I2C_CR1_STOP;

				// Clear the flags
				(void) i2cHandle->SR1;
				(void) i2cHandle->SR2;

				tickStart = System::GetTick();
				while (i2cHandle->SR2 & I2C_SR2_BUSY) {
					// If BUSY is still set and the time is up, this is an error
					if (System::GetTick() > (tickStart + timeout)) {
						return ResultStatus::busy;
					}
				}
				return ResultStatus::ok;
			}

			// No acknowledge: send STOP and clear the AF flag
			i2cHandle->CR1 |= I2C_CR1_STOP;
			i2cHandle->SR1 &= ~I2C_SR1_AF;

			tickStart = System::GetTick();
			while (i2cHandle->SR2 & I2C_SR2_BUSY) {
				// If BUSY is still set and the time is up, this is an error
				if (System::GetTick() > (tickStart + timeout)) {
					return ResultStatus::busy;
				}
			}

			repeatCount++;
		} while (repeatCount < repeat);

		return ResultStatus::error;
	}



	virtual ResultStatus CheckDeviceAsync(uint8, uint16) override {
		return ResultStatus::notSupported;
	}



	// Probes the addresses 0x08 to 0x77. The other addresses are reserved by the I2C specification (UM10204). The scan stops
	// when the list is full.
	virtual Result<uint8> Scan(uint8 *listBuffer, uint8 size) override {
		uint8 count = 0;

		for (uint8 address = 0x08; address <= 0x77 && count < size; address++) {
			if (CheckDevice(address, 1) == ResultStatus::ok) {
				listBuffer[count] = address;
				count++;
			}
		}

		return Ok<uint8>(count);
	}



	virtual Result<uint8> ScanAsync(uint8 *, uint8) override {
		return { ResultStatus::notSupported };
	}



	virtual ResultStatus WriteByteArray(uint8 device, uint16 address, uint8 addressSize, uint8 *writeData, uint32 dataSize) override {
		if (addressSize == 0 || addressSize > 2) {
			return ResultStatus::invalidParameter;
		}

		if (isBusyFlag(SET) != ResultStatus::ok) {
			return ResultStatus::busy;
		}

		if ((i2cHandle->CR1 & I2C_CR1_PE) != I2C_CR1_PE) {
			i2cHandle->CR1 |= I2C_CR1_PE;
		}

		i2cHandle->CR1 &= ~I2C_CR1_POS;

		ResultStatus status = RequestMemoryWrite(device, address, addressSize);
		if (status != ResultStatus::ok) {
			return status;
		}

		while (dataSize > 0) {
			if (isTXE_Flag() == ResultStatus::error) {
				i2cHandle->CR1 |= I2C_CR1_STOP;
				return ResultStatus::error;
			}
			i2cHandle->DR = static_cast<uint8>(*writeData);
			writeData++;
			dataSize--;

			if ((isBTF_Flag(SET) != ResultStatus::ok) && (dataSize != 0)) {
				i2cHandle->DR = static_cast<uint8>(*writeData);
				writeData++;
				dataSize--;
			}
		}

		status = isBTF_FlagTimeOut();
		if (status != ResultStatus::ok) {
			if (status == ResultStatus::error) {
				i2cHandle->CR1 |= I2C_CR1_STOP;
			}
			return ResultStatus::error;
		}
		i2cHandle->CR1 |= I2C_CR1_STOP;
		return ResultStatus::ok;
	}



	virtual ResultStatus ReadByteArray(uint8 device, uint16 address, uint8 addressSize, uint8 *readData, uint32 dataSize) override {
		uint32 count = dataSize;

		if (addressSize == 0 || addressSize > 2) {
			return ResultStatus::invalidParameter;
		}

		if (isBusyFlag(SET) != ResultStatus::ok) {
			return ResultStatus::busy;
		}

		if ((i2cHandle->CR1 & I2C_CR1_PE) != I2C_CR1_PE) {
			i2cHandle->CR1 |= I2C_CR1_PE;
		}

		i2cHandle->CR1 &= ~I2C_CR1_POS;

		ResultStatus status = RequestMemoryRead(device, address, addressSize);
		if (status != ResultStatus::ok) {
			return status;
		}

		if (dataSize == 0) {
			(void) (i2cHandle->SR1);
			(void) (i2cHandle->SR2);

			i2cHandle->CR1 |= I2C_CR1_STOP;
		} else if (dataSize == 1) {
			i2cHandle->CR1 &= ~I2C_CR1_ACK;

			(void) (i2cHandle->SR1);
			(void) (i2cHandle->SR2);

			i2cHandle->CR1 |= I2C_CR1_STOP;
		} else if (dataSize == 2) {
			i2cHandle->CR1 &= ~I2C_CR1_ACK;

			i2cHandle->CR1 |= I2C_CR1_POS;

			(void) (i2cHandle->SR1);
			(void) (i2cHandle->SR2);
		} else {
			(void) (i2cHandle->SR1);
			(void) (i2cHandle->SR2);
		}


		while (count > 0) {
			if (count <= 3) {

				if (count == 1) {
					if (isRXNE_Flag() != ResultStatus::ok) {
						return ResultStatus::error;
					}
					*readData = static_cast<uint8>(i2cHandle->DR);
					readData++;
					count--;
				} else if (count == 2) {
					if (isBTF_Flag(RESET) != ResultStatus::ok) {
						return ResultStatus::error;
					}
					i2cHandle->CR1 |= I2C_CR1_STOP;
					*readData = static_cast<uint8>(i2cHandle->DR);
					readData++;
					count--;
					*readData = static_cast<uint8>(i2cHandle->DR);
					readData++;
					count--;
				} else { // Three bytes
					if (isBTF_Flag(RESET) != ResultStatus::ok) {
						return ResultStatus::error;
					}
					i2cHandle->CR1 &= ~I2C_CR1_ACK;
					*readData = static_cast<uint8>(i2cHandle->DR);
					readData++;
					count--;
					if (isBTF_Flag(RESET) != ResultStatus::ok) {
						return ResultStatus::error;
					}
					i2cHandle->CR1 |= I2C_CR1_STOP;
					*readData = static_cast<uint8>(i2cHandle->DR);
					readData++;
					count--;
					*readData = static_cast<uint8>(i2cHandle->DR);
					readData++;
					count--;
				}
			} else {
				if (isRXNE_Flag() != ResultStatus::ok) {
					return ResultStatus::error;
				}
				*readData = static_cast<uint8>(i2cHandle->DR);
				readData++;
				count--;
				if (((i2cHandle->SR1 & I2C_SR1_BTF) >> I2C_SR1_BTF_Pos) == SET) {
					*readData = static_cast<uint8>(i2cHandle->DR);
					readData++;
					count--;
				}
			}
		}
		return ResultStatus::ok;
	}



	virtual ResultStatus WriteByteArrayAsync(uint8 device, uint16 address, uint8 addressSize, uint8 *writeData, uint32 dataSize) override {
		if (addressSize > 2 || (addressSize == 0 && dataSize == 0)) {
			return ResultStatus::invalidParameter;
		}

		return StartAsync(device, address, addressSize, writeData, dataSize, false);
	}



	virtual ResultStatus ReadByteArrayAsync(uint8 device, uint16 address, uint8 addressSize, uint8 *readData, uint32 dataSize) override {
		if (addressSize > 2 || dataSize == 0) {
			return ResultStatus::invalidParameter;
		}

		return StartAsync(device, address, addressSize, readData, dataSize, true);
	}



protected:
	virtual ResultStatus Initialization() override {
		// FastPlus is refused before the user's pre-initialization hook runs, since nothing is set up for it.
		if(parameters.speed == Speed::FastPlus) {
			return ResultStatus::notSupported;
		}

		ResultStatus status = BeforeInitialization();
		if(status != ResultStatus::ok) {
			return status;
		}

		// The clock speed is given in hertz. The speed option is in kilohertz.
		LL_I2C_InitTypeDef init = {
			.PeripheralMode = LL_I2C_MODE_I2C,
			.ClockSpeed = static_cast<uint32>(parameters.speed) * 1000U,
			.DutyCycle = parameters.dutyCycle == DutyCycle::D16_9 ? LL_I2C_DUTYCYCLE_16_9 : LL_I2C_DUTYCYCLE_2,
			.OwnAddress1 = parameters.slaveAddress,
			.TypeAcknowledge = LL_I2C_ACK,
			.OwnAddrSize = LL_I2C_OWNADDRESS1_7BIT
		};

		LL_I2C_Init(i2cHandle, &init);

		LL_I2C_SetOwnAddress2(i2cHandle, 0);
		LL_I2C_DisableOwnAddress2(i2cHandle);
		LL_I2C_DisableGeneralCall(i2cHandle);

		if(parameters.clockStretching) {
			LL_I2C_EnableClockStretching(i2cHandle);
		} else {
			LL_I2C_DisableClockStretching(i2cHandle);
		}

		return AfterInitialization();
	}



private:
	inline void SlaveIrqEvent() {

	}



	// Starts an asynchronous transfer. A transfer that failed leaves the state at error, and a new transfer may start from it.
	ResultStatus StartAsync(uint8 device, uint16 address, uint8 addressSize, uint8 *buffer, uint32 size, bool isRead) {
		if (state == ResultStatus::busy) {
			return ResultStatus::busy;
		}

		state = ResultStatus::busy;
		deviceAddress = device;
		registerAddress = address;
		registerAddressSize = addressSize;
		registerBytesSent = 0;
		isReadTransfer = isRead;

		if (isRead) {
			rxDataNeed = size;
			rxDataCounter = 0;
			rxDataPointer = buffer;
			txDataNeed = 0;
			txDataCounter = 0;
			txDataPointer = nullptr;
			masterPhase = addressSize == 0 ? MasterPhase::ReadAddress : MasterPhase::WriteAddress;
		} else {
			rxDataNeed = 0;
			rxDataCounter = 0;
			rxDataPointer = nullptr;
			txDataNeed = size;
			txDataCounter = 0;
			txDataPointer = buffer;
			masterPhase = MasterPhase::WriteAddress;
		}

		if (!LL_I2C_IsEnabled(i2cHandle)) {
			LL_I2C_Enable(i2cHandle);
		}

		LL_I2C_DisableBitPOS(i2cHandle);
		LL_I2C_AcknowledgeNextData(i2cHandle, LL_I2C_ACK);
		LL_I2C_GenerateStartCondition(i2cHandle);

		LL_I2C_EnableIT_EVT(i2cHandle);
		LL_I2C_EnableIT_BUF(i2cHandle);
		LL_I2C_EnableIT_ERR(i2cHandle);

		return ResultStatus::ok;
	}



	void MasterIrqEvent() {
		if (LL_I2C_IsActiveFlag_SB(i2cHandle)) {
			SendDeviceAddress();
			return;
		}

		if (LL_I2C_IsActiveFlag_ADDR(i2cHandle)) {
			AddressAcknowledged();
			return;
		}

		switch (masterPhase) {
			case MasterPhase::Transmit:
				if (LL_I2C_IsActiveFlag_BTF(i2cHandle)) {
					TransmitBtf();
				} else if (LL_I2C_IsActiveFlag_TXE(i2cHandle)) {
					TransmitNext();
				}
			break;

			case MasterPhase::Receive:
				if (LL_I2C_IsActiveFlag_BTF(i2cHandle)) {
					ReceiveBtf();
				} else if (LL_I2C_IsActiveFlag_RXNE(i2cHandle)) {
					ReceiveNext();
				}
			break;

			default:
			break;
		}
	}



	// The device address goes out with the R/W bit: clear for write, set for read.
	void SendDeviceAddress() {
		if (masterPhase == MasterPhase::WriteAddress) {
			LL_I2C_TransmitData8(i2cHandle, static_cast<uint8>((deviceAddress << 1) & ~1U));
		} else if (masterPhase == MasterPhase::ReadAddress) {
			LL_I2C_TransmitData8(i2cHandle, static_cast<uint8>((deviceAddress << 1) | 1U));
		}
	}



	void AddressAcknowledged() {
		if (masterPhase == MasterPhase::WriteAddress) {
			LL_I2C_ClearFlag_ADDR(i2cHandle);
			masterPhase = MasterPhase::Transmit;
			return;
		}

		if (masterPhase == MasterPhase::ReadAddress) {
			PrepareReceive();
			masterPhase = MasterPhase::Receive;
			return;
		}

		// An address flag outside the address phases is stale. Clearing it stops the interrupt from firing again.
		LL_I2C_ClearFlag_ADDR(i2cHandle);
	}



	// Sets ACK, POS and the buffer interrupt for the number of bytes to read. The reference manual gives the order: for one
	// byte, NACK and then STOP after ADDR is cleared; for two bytes, NACK and POS before ADDR is cleared. For three or more,
	// the last three bytes are taken in the BTF handler, so the buffer interrupt is off for them.
	void PrepareReceive() {
		uint32 remaining = rxDataNeed - rxDataCounter;

		if (remaining == 1) {
			LL_I2C_AcknowledgeNextData(i2cHandle, LL_I2C_NACK);
			LL_I2C_EnableIT_BUF(i2cHandle);
		} else if (remaining == 2) {
			LL_I2C_AcknowledgeNextData(i2cHandle, LL_I2C_NACK);
			LL_I2C_EnableBitPOS(i2cHandle);
			LL_I2C_DisableIT_BUF(i2cHandle);
		} else if (remaining == 3) {
			LL_I2C_DisableIT_BUF(i2cHandle);
		} else {
			LL_I2C_EnableIT_BUF(i2cHandle);
		}

		LL_I2C_ClearFlag_ADDR(i2cHandle);

		if (remaining == 1) {
			LL_I2C_GenerateStopCondition(i2cHandle);
		}
	}



	// Sends the register address (high byte first), then the data. The last byte leaves the buffer empty, and the
	// interrupt is turned off there. BTF then ends a write with STOP, or a read with a repeated START.
	void TransmitNext() {
		if (!HasPendingTransmit()) {
			LL_I2C_DisableIT_BUF(i2cHandle);
			return;
		}

		LL_I2C_TransmitData8(i2cHandle, NextTransmitByte());

		if (!HasPendingTransmit()) {
			LL_I2C_DisableIT_BUF(i2cHandle);
		}
	}



	void TransmitBtf() {
		if (HasPendingTransmit()) {
			LL_I2C_TransmitData8(i2cHandle, NextTransmitByte());
			return;
		}

		if (isReadTransfer) {
			LL_I2C_GenerateStartCondition(i2cHandle);
			masterPhase = MasterPhase::ReadAddress;
			return;
		}

		LL_I2C_GenerateStopCondition(i2cHandle);
		CompleteTransfer();
	}



	void ReceiveNext() {
		uint32 remaining = rxDataNeed - rxDataCounter;

		if (remaining == 1) {
			ReadDataByte();
			CompleteTransfer();
			return;
		}

		if (remaining > 3) {
			ReadDataByte();
			if (rxDataNeed - rxDataCounter == 3) {
				LL_I2C_DisableIT_BUF(i2cHandle);
			}
			return;
		}

		// Two or three bytes are left to the BTF handler. The buffer interrupt is already off, so this is not reached.
		LL_I2C_DisableIT_BUF(i2cHandle);
	}



	// Reference manual sequence for the last bytes: with three left, NACK and read one; with two left, STOP and read both.
	void ReceiveBtf() {
		uint32 remaining = rxDataNeed - rxDataCounter;

		if (remaining == 3) {
			LL_I2C_AcknowledgeNextData(i2cHandle, LL_I2C_NACK);
			ReadDataByte();
			return;
		}

		if (remaining == 2) {
			LL_I2C_GenerateStopCondition(i2cHandle);
			ReadDataByte();
			ReadDataByte();
			CompleteTransfer();
			return;
		}

		ReadDataByte();
		if (rxDataNeed - rxDataCounter == 0) {
			CompleteTransfer();
		}
	}



	void ReadDataByte() {
		*rxDataPointer = LL_I2C_ReceiveData8(i2cHandle);
		rxDataPointer++;
		rxDataCounter++;
	}



	ResultStatus ReadState() const {
		return *static_cast<const volatile ResultStatus *>(&state);
	}



	bool HasPendingTransmit() const {
		return registerBytesSent < registerAddressSize || txDataCounter < txDataNeed;
	}



	uint8 NextTransmitByte() {
		if (registerBytesSent < registerAddressSize) {
			uint8 shift = static_cast<uint8>(8 * (registerAddressSize - 1 - registerBytesSent));
			registerBytesSent++;
			return static_cast<uint8>(registerAddress >> shift);
		}

		uint8 value = *txDataPointer;
		txDataPointer++;
		txDataCounter++;
		return value;
	}



	void CompleteTransfer() {
		EndTransfer(ResultStatus::ready);

		if (onComplete != nullptr) {
			onComplete();
		}
	}



	// Turns the transfer interrupts off and sets the state the transfer ends in: ready, or error after a failed bus event.
	void EndTransfer(ResultStatus endState) {
		LL_I2C_DisableIT_EVT(i2cHandle);
		LL_I2C_DisableIT_BUF(i2cHandle);
		LL_I2C_DisableIT_ERR(i2cHandle);

		state = endState;
	}



	ResultStatus isBusyFlag(bool flagSet) {
		uint64 tickStart = System::GetTick();
		while (((i2cHandle->SR2 & I2C_SR2_BUSY) >> I2C_SR2_BUSY_Pos) == flagSet) {
			if ((System::GetTick() - tickStart) > timeout) {
				return ResultStatus::timeout;
			}
		}
		return ResultStatus::ok;
	}



	ResultStatus isSB_Flag(bool flagSet) {
		uint64 tickStart = System::GetTick();
		while (((i2cHandle->SR1 & I2C_SR1_SB) >> I2C_SR1_SB_Pos) == flagSet) {
			if ((System::GetTick() - tickStart) > timeout) {
				return ResultStatus::timeout;
			}
		}
		return ResultStatus::ok;
	}



	ResultStatus isAddressAF_Flag() {
		uint64 tickStart = System::GetTick();
		while (((i2cHandle->SR1 & I2C_SR1_ADDR) >> I2C_SR1_ADDR_Pos) == RESET) {
			if (((i2cHandle->SR1 & I2C_SR1_AF) >> I2C_SR1_AF_Pos) == SET) {
				i2cHandle->CR1 |= I2C_CR1_STOP;
				i2cHandle->SR1 &= ~I2C_SR1_AF;
				return ResultStatus::error;
			}

			if ((System::GetTick() - tickStart) > timeout) {
				return ResultStatus::timeout;
			}
		}
		return ResultStatus::ok;
	}



	ResultStatus isTXE_Flag() {
		uint64 tickStart = System::GetTick();
		while (((i2cHandle->SR1 & I2C_SR1_TXE) >> I2C_SR1_TXE_Pos) == RESET) {
			if (((i2cHandle->SR1 & I2C_SR1_AF) >> I2C_SR1_AF_Pos) == SET) {
				i2cHandle->SR1 &= ~I2C_SR1_AF;
				return ResultStatus::error;
			}

			if ((System::GetTick() - tickStart) > timeout) {
				return ResultStatus::timeout;
			}
		}
		return ResultStatus::ok;
	}



	ResultStatus isRXNE_Flag() {
		uint64 tickStart = System::GetTick();
		while (((i2cHandle->SR1 & I2C_SR1_RXNE) >> I2C_SR1_RXNE_Pos) == RESET) {
			if (((i2cHandle->SR1 & I2C_SR1_STOPF) >> I2C_SR1_STOPF_Pos) == SET) {
				i2cHandle->SR1 &= ~I2C_SR1_STOPF;
				return ResultStatus::error;
			}

			if ((System::GetTick() - tickStart) > timeout) {
				return ResultStatus::timeout;
			}
		}
		return ResultStatus::ok;
	}



	ResultStatus isBTF_Flag(bool flagSet) {
		uint64 tickStart = System::GetTick();
		while (((i2cHandle->SR1 & I2C_SR1_BTF) >> I2C_SR1_BTF_Pos) == flagSet) {
			if ((System::GetTick() - tickStart) > timeout) {
				return ResultStatus::timeout;
			}
		}
		return ResultStatus::ok;
	}



	ResultStatus isBTF_FlagTimeOut() {
		uint64 tickStart = System::GetTick();
		while (((i2cHandle->SR1 & I2C_SR1_BTF) >> I2C_SR1_BTF_Pos) == RESET) {
			if (((i2cHandle->SR1 & I2C_SR1_AF) >> I2C_SR1_AF_Pos) != 0) {
				return ResultStatus::error;
			}

			if ((System::GetTick() - tickStart) > timeout) {
				return ResultStatus::timeout;
			}
		}
		return ResultStatus::ok;
	}



	// Write phase of a read: START, device address for write, and the register address. Then BTF is waited for, so the last
	// register byte has left the shift register, and a repeated START sends the device address for read. The reference manual
	// gives this order for both register sizes.
	ResultStatus RequestMemoryRead(uint8 deviceAddress, uint16 memAddress, uint8 memAddressSize) {
		i2cHandle->CR1 |= I2C_CR1_ACK;

		ResultStatus status = RequestMemoryWrite(deviceAddress, memAddress, memAddressSize);
		if (status != ResultStatus::ok) {
			return status;
		}

		status = isBTF_FlagTimeOut();
		if (status != ResultStatus::ok) {
			if (status == ResultStatus::error) {
				i2cHandle->CR1 |= I2C_CR1_STOP;
			}
			return status;
		}

		i2cHandle->CR1 |= I2C_CR1_START;

		if (isSB_Flag(RESET) != ResultStatus::ok) {
			if ((i2cHandle->CR1 & I2C_CR1_START) == I2C_CR1_START) {
				return ResultStatus::error;
			}
			return ResultStatus::timeout;
		}

		i2cHandle->DR = static_cast<uint8>((deviceAddress << 1) | 1U);

		return isAddressAF_Flag();
	}



	ResultStatus RequestMemoryWrite(uint8 deviceAddress, uint16 memAddress, uint8 memAddressSize) {
		i2cHandle->CR1 |= I2C_CR1_START;

		if (isSB_Flag(RESET) != ResultStatus::ok) {
			if ((i2cHandle->CR1 & I2C_CR1_START) == I2C_CR1_START) {
				return ResultStatus::error;
			}
			return ResultStatus::timeout;
		}

		i2cHandle->DR = static_cast<uint8>((deviceAddress << 1) & ~1U);

		ResultStatus status = isAddressAF_Flag();
		if (status != ResultStatus::ok) {
			return status;
		}

		(void) i2cHandle->SR1;
		(void) i2cHandle->SR2;

		status = isTXE_Flag();
		if (status != ResultStatus::ok) {
			if (status == ResultStatus::error) {
				i2cHandle->CR1 |= I2C_CR1_STOP;
			}
			return status;
		}

		if (memAddressSize == 1) {
			i2cHandle->DR = memAddress & 0xFF;
		} else {
			i2cHandle->DR = (memAddress >> 8) & 0xFF;

			status = isTXE_Flag();
			if (status != ResultStatus::ok) {
				if (status == ResultStatus::error) {
					i2cHandle->CR1 |= I2C_CR1_STOP;
				}
				return status;
			}

			i2cHandle->DR = memAddress & 0xFF;

			status = isTXE_Flag();
			if (status != ResultStatus::ok) {
				if (status == ResultStatus::error) {
					i2cHandle->CR1 |= I2C_CR1_STOP;
				}
				return status;
			}
		}
		return ResultStatus::ok;
	}
};
