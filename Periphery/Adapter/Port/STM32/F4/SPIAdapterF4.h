#pragma once
#include <Adapter/SPIAdapter.h>



using ASPI = class SPIAdapterF4;


// SPI on the F4 peripheral. The BSP enables the instance clock before SetParameters() and forwards SPIx_IRQn to IrqHandler().
//
// Asynchronous reads run the clock: a read starts a transmit of dummy bytes (0xFF) with the receive, so a master gives the
// clock the slave needs. Abort functions stop the whole frame stream, both directions, because the two share one clock.
// Continuous receive is started with StartContinuousAsyncRxMode() and stopped with StopContinuousAsyncRxMode().
//
// Not supported: continuous transmit (SetContinuousAsyncTxMode), CRC, and transfers larger than 65535 bytes, which the base
// class counts in 16 bits.


class SPIAdapterF4: public SPIAdapter<SPI_TypeDef> {
private:
	// The base class counts transfers in 16 bits. A longer transfer is refused, not cut to its low 16 bits.
	constexpr static uint32 maxTransferSize = 0xFFFFU;


	struct BaudRatePrescaler {
		uint32 value;
		uint16 prescaler;
	};



public:
	SPIAdapterF4() { }

	SPIAdapterF4(SPI_TypeDef *spi, uint32 busClock = 0):SPIAdapter(spi, busClock) {

	}



	virtual void AbortReceive() override {
		AbortAllTransfers();
	}



	virtual void AbortTransmit() override {
		AbortAllTransfers();
	}



	// Errors are served before data, so an overrun is cleared before the receive handler looks at RXNE.
	virtual void IrqHandler() override {
		ErrorInterrupt();
		ReceiveInterrupt();
		TransmitInterrupt();
	}



	// Parameters are only installed while the instance is idle. The base class would store them before Initialization()
	// refuses a busy instance, and an in-flight transfer would then run with settings it did not start with.
	virtual Result<uint32> SetParameters(Parameters val) override {
		if(txState != ResultStatus::ready || rxState != ResultStatus::ready) {
			return ResultStatus::busy;
		}

		return SPIAdapter::SetParameters(val);
	}





protected:
	virtual ResultStatus Initialization() override {
		ResultStatus status = BeforeInitialization();
		if(status != ResultStatus::ok) {
			return status;
		}

		LL_SPI_InitTypeDef init = {
			.TransferDirection = CastTransferDirection(),
			.Mode = CastMode(),
			.DataWidth = LL_SPI_DATAWIDTH_8BIT,
			.ClockPolarity = CastClockPolarity(),
			.ClockPhase = CastClockPhase(),
			.NSS = LL_SPI_NSS_SOFT,
			.BaudRate = baudRatePrescaler,
			.BitOrder = CastBitOrder(),
			.CRCCalculation = LL_SPI_CRCCALCULATION_DISABLE,
			.CRCPoly = 10
		};

		if(LL_SPI_Init(spiHandle, &init) != ErrorStatus::SUCCESS) {
			return ResultStatus::error;
		}

		LL_SPI_SetStandard(spiHandle, LL_SPI_PROTOCOL_MOTOROLA);

		// Enable SPI once during initialization
		LL_SPI_Enable(spiHandle);

		return AfterInitialization();
	}




	uint32 CalculatePrescaler() override {
		static const uint8 baudArraySize = 8;

		static const BaudRatePrescaler baudRatePrescalers[baudArraySize] = {
			{LL_SPI_BAUDRATEPRESCALER_DIV2, 	2},
			{LL_SPI_BAUDRATEPRESCALER_DIV4, 	4},
			{LL_SPI_BAUDRATEPRESCALER_DIV8, 	8},
			{LL_SPI_BAUDRATEPRESCALER_DIV16, 	16},
			{LL_SPI_BAUDRATEPRESCALER_DIV32, 	32},
			{LL_SPI_BAUDRATEPRESCALER_DIV64, 	64},
			{LL_SPI_BAUDRATEPRESCALER_DIV128, 	128},
			{LL_SPI_BAUDRATEPRESCALER_DIV256, 	256}
		};

		if(parameters.maxSpeedHz == 0 || inputBusClockHz == 0) {
			return baudRatePrescalers[baudArraySize - 1].value;
		}

		for (uint8 i = 0; i < baudArraySize; i++) {
			uint32 clock = inputBusClockHz / baudRatePrescalers[i].prescaler;

			if(clock <= parameters.maxSpeedHz) {
				baudRatePrescaler = baudRatePrescalers[i].value;
				return clock;
			}
		}

		return baudRatePrescalers[baudArraySize - 1].value;
	}




	virtual ResultStatus StartContinuousAsyncRxMode() {
		if (txState != ResultStatus::ready || rxState != ResultStatus::ready) {
			return ResultStatus::busy;
		}

		// The mode is set before the interrupts are enabled, so a byte that arrives at once takes the continuous path.
		continuousAsyncRxMode = true;
		rxState = ResultStatus::busy;
		txDataNeed = 0;
		txDataPointer = 0;
		txDataCounter = 0;

		LL_SPI_EnableIT_RXNE(spiHandle);
		LL_SPI_EnableIT_ERR(spiHandle);

		// SPI is already enabled during initialization

		return ResultStatus::ok;
	}





	// Stops the continuous receive only. A write in progress keeps its interrupts and state. Outside continuous receive there
	// is nothing to stop, and a one-shot read in progress is left alone.
	virtual ResultStatus StopContinuousAsyncRxMode() {
		if (!continuousAsyncRxMode) {
			return ResultStatus::ok;
		}

		uint32 primask = __get_PRIMASK();
		__disable_irq();
		LL_SPI_DisableIT_RXNE(spiHandle);
		rxState = ResultStatus::ready;
		rxDataPointer = nullptr;
		rxDataNeed = 0;
		rxDataCounter = 0;
		if (txState == ResultStatus::ready) {
			LL_SPI_DisableIT_ERR(spiHandle);
		}
		__set_PRIMASK(primask);

		return ResultStatus::ok;
	}





	inline virtual ResultStatus WriteByteArray(uint8 *buffer, uint32 size) override {
		if (size > maxTransferSize) {
			return ResultStatus::invalidParameter;
		}

		if (txState != ResultStatus::ready) {
			return ResultStatus::busy;
		}

		txState = ResultStatus::busy;
		txDataNeed = size;
		txDataPointer = buffer;
		txDataCounter = 0;
		uint32 tickStart = System::GetTick();


		// SPI is already enabled during initialization


		if (parameters.mode == Mode::Slave || size == 1) {
			LL_SPI_TransmitData8(spiHandle, *txDataPointer++);
			txDataCounter++;
		}


		for (; txDataCounter < txDataNeed; txDataCounter++) {
			while(!LL_SPI_IsActiveFlag_TXE(spiHandle)) {
				if((System::GetTick() - tickStart) > timeout) {
					// Keep SPI enabled even on timeout
					txState = ResultStatus::ready;
					return ResultStatus::timeout;
				}
			}

			LL_SPI_TransmitData8(spiHandle, *txDataPointer++);
		}


		if (parameters.direction == Direction::TxRx) {
			LL_SPI_ClearFlag_OVR(spiHandle);
		}

		// Keep SPI enabled between transfers
		txState = ResultStatus::ready;

		return ResultStatus::ok;
	}





	inline virtual ResultStatus ReadByteArray(uint8 *buffer, uint32 size) override {
		if (size > maxTransferSize) {
			return ResultStatus::invalidParameter;
		}

		if (rxState != ResultStatus::ready) {
			return ResultStatus::busy;
		}

		rxState = ResultStatus::busy;
		rxDataNeed = size;
		rxDataPointer = buffer;
		uint32 tickStart = System::GetTick();


		// SPI is already enabled during initialization


		for (rxDataCounter = 0; rxDataCounter < rxDataNeed; rxDataCounter++) {
			// A master receives only while it clocks the bus, so each byte is sent as 0xFF before its reply is read.
			if (parameters.mode == Mode::Master) {
				while(!LL_SPI_IsActiveFlag_TXE(spiHandle)) {
					if((System::GetTick() - tickStart) > timeout) {
						rxState = ResultStatus::ready;
						return ResultStatus::timeout;
					}
				}

				LL_SPI_TransmitData8(spiHandle, 0xFF);
			}

			while(!LL_SPI_IsActiveFlag_RXNE(spiHandle)) {
				if((System::GetTick() - tickStart) > timeout) {
					// Keep SPI enabled even on timeout
					rxState = ResultStatus::ready;
					return ResultStatus::timeout;
				}
			}

			lastRxData = LL_SPI_ReceiveData8(spiHandle);
			*rxDataPointer++ = lastRxData;
		}


		// Keep SPI enabled between transfers
		rxState = ResultStatus::ready;

		return ResultStatus::ok;
	}





	inline virtual ResultStatus WriteReadByteArray(uint8 *txBuffer, uint8 *rxBuffer, uint32 size) override {
		if (size > maxTransferSize) {
			return ResultStatus::invalidParameter;
		}

		if (txState != ResultStatus::ready || rxState != ResultStatus::ready) {
			return ResultStatus::busy;
		}

		txState = ResultStatus::busy;
		txDataNeed = size;
		txDataPointer = txBuffer;
		txDataCounter = 0;

		rxState = ResultStatus::busy;
		rxDataNeed = size;
		rxDataPointer = rxBuffer;
		rxDataCounter = 0;
		uint32 tickStart = System::GetTick();


		// SPI is already enabled during initialization


		if (parameters.mode == Mode::Slave || size == 1) {
			LL_SPI_TransmitData8(spiHandle, *txDataPointer++);
			txDataCounter++;
		}


		bool isTxAllowed = true;

		while (txDataCounter < txDataNeed || rxDataCounter < rxDataNeed) {
			if (LL_SPI_IsActiveFlag_TXE(spiHandle) && txDataCounter < txDataNeed && isTxAllowed) {
				LL_SPI_TransmitData8(spiHandle, *txDataPointer++);
				txDataCounter++;
				isTxAllowed = false;
			}

			if (LL_SPI_IsActiveFlag_RXNE(spiHandle) && rxDataCounter < rxDataNeed) {
				lastRxData = LL_SPI_ReceiveData8(spiHandle);
				*rxDataPointer++ = lastRxData;
				rxDataCounter++;
				isTxAllowed = true;
			}

			if((System::GetTick() - tickStart) > timeout) {
				// Keep SPI enabled even on timeout
				txState = ResultStatus::ready;
				rxState = ResultStatus::ready;
				return ResultStatus::timeout;
			}
		}


		if (parameters.direction == Direction::TxRx) {
			LL_SPI_ClearFlag_OVR(spiHandle);
		}

		// Keep SPI enabled between transfers

		txState = ResultStatus::ready;
		rxState = ResultStatus::ready;

		return ResultStatus::ok;
	}





	inline virtual ResultStatus WriteByteArrayAsync(uint8 *buffer, uint32 size) override {
		if (size == 0 || size > maxTransferSize) {
			return ResultStatus::invalidParameter;
		}

		if (txState != ResultStatus::ready) {
			return ResultStatus::busy;
		}

		txState = ResultStatus::busy;
		txDataNeed = size;
		txDataPointer = buffer;
		txDataCounter = 0;

		LL_SPI_EnableIT_TXE(spiHandle);
		LL_SPI_EnableIT_ERR(spiHandle);

		// SPI is already enabled during initialization

		return ResultStatus::ok;
	}





	// Receives into the buffer and clocks out dummy bytes at the same time. The transmit counter follows the receive count,
	// and both directions are busy until each one has finished.
	inline virtual ResultStatus ReadByteArrayAsync(uint8 *buffer, uint32 size) override {
		if (size == 0 || size > maxTransferSize) {
			return ResultStatus::invalidParameter;
		}

		if (rxState != ResultStatus::ready || txState != ResultStatus::ready) {
			return ResultStatus::busy;
		}

		rxState = ResultStatus::busy;
		rxDataNeed = size;
		rxDataCounter = 0;
		rxDataPointer = buffer;

		txState = ResultStatus::busy;
		txDataNeed = size;
		txDataCounter = 0;
		txDataPointer = nullptr;

		LL_SPI_EnableIT_RXNE(spiHandle);
		LL_SPI_EnableIT_TXE(spiHandle);
		LL_SPI_EnableIT_ERR(spiHandle);

		if(!LL_SPI_IsEnabled(spiHandle)) {
			LL_SPI_Enable(spiHandle);
		}

		return ResultStatus::ok;
	}





	inline virtual ResultStatus WriteReadByteArrayAsync(uint8 *txBuffer, uint8 *rxBuffer, uint32 size) override {
		if (size == 0 || size > maxTransferSize) {
			return ResultStatus::invalidParameter;
		}

		if (txState != ResultStatus::ready || rxState != ResultStatus::ready) {
			return ResultStatus::busy;
		}

		txState = ResultStatus::busy;
		txDataNeed = size;
		txDataPointer = txBuffer;
		txDataCounter = 0;

		rxState = ResultStatus::busy;
		rxDataNeed = size;
		rxDataPointer = rxBuffer;
		rxDataCounter = 0;

		LL_SPI_EnableIT_TXE(spiHandle);
		LL_SPI_EnableIT_RXNE(spiHandle);
		LL_SPI_EnableIT_ERR(spiHandle);

		if(!LL_SPI_IsEnabled(spiHandle)) {
			LL_SPI_Enable(spiHandle);
		}

		return ResultStatus::ok;
	}





private:
	// Reads and clears the error flags. Each flag is reported when its error interrupt is enabled. The flags are cleared
	// either way, so a flag left set cannot block the receive handler.
	inline void ErrorInterrupt() {
		bool errorInterruptEnabled = LL_SPI_IsEnabledIT_ERR(spiHandle);

		if(LL_SPI_IsActiveFlag_MODF(spiHandle)) {
			// LL_SPI_ClearFlag_MODF clears SPE as well, so the instance is enabled again or it stays silent for good.
			LL_SPI_ClearFlag_MODF(spiHandle);
			LL_SPI_Enable(spiHandle);

			if(errorInterruptEnabled) {
				CallError(Error::ModeFault);
			}
		}

		if(LL_SPI_IsActiveFlag_FRE(spiHandle)) {
			LL_SPI_ClearFlag_FRE(spiHandle);

			if(errorInterruptEnabled) {
				CallError(Error::FrameFormat);
			}
		}

		// An overrun is a fault only while a receive waits for its byte. A write does not read the receive register, so the
		// overrun it causes is expected. The flag is cleared either way.
		if(LL_SPI_IsActiveFlag_OVR(spiHandle)) {
			LL_SPI_ClearFlag_OVR(spiHandle);

			if(errorInterruptEnabled && rxState == ResultStatus::busy) {
				CallError(Error::Overrun);
			}
		}

		// CRC is not enabled by Initialization(), so this flag cannot be raised. Clearing it keeps a stray flag from
		// holding the error interrupt. The base error enum has no CRC entry, so it is not reported.
		if(LL_SPI_IsActiveFlag_CRCERR(spiHandle)) {
			LL_SPI_ClearFlag_CRCERR(spiHandle);
		}
	}



	inline void ReceiveInterrupt() {
		if (LL_SPI_IsActiveFlag_OVR(spiHandle) || !LL_SPI_IsActiveFlag_RXNE(spiHandle) || !LL_SPI_IsEnabledIT_RXNE(spiHandle)) {
			return;
		}

		lastRxData = LL_SPI_ReceiveData8(spiHandle);

		if(continuousAsyncRxMode) {
			CallInterrupt(Irq::Rx);
			return;
		}

		*rxDataPointer++ = lastRxData;

		if (++rxDataCounter < rxDataNeed) {
			return;
		}

		if(txState == ResultStatus::ready) {
			LL_SPI_DisableIT_ERR(spiHandle);
		}
		LL_SPI_DisableIT_RXNE(spiHandle);

		rxState = ResultStatus::ready;

		CallInterrupt(Irq::Rx);
	}



	// A transmit with no data pointer is the clock of a read: it sends dummy bytes and reports no Tx event.
	inline void TransmitInterrupt() {
		if (!LL_SPI_IsEnabledIT_TXE(spiHandle) || !LL_SPI_IsActiveFlag_TXE(spiHandle)) {
			return;
		}

		if(continuousAsyncTxMode) {
			SystemAbort(); // TODO: continuous
			return;
		}

		bool isDummy = txDataPointer == nullptr;
		uint8 frame = 0xFF;

		if(!isDummy) {
			frame = *txDataPointer;
			txDataPointer++;
		}

		LL_SPI_TransmitData8(spiHandle, frame);

		if (++txDataCounter < txDataNeed) {
			return;
		}

		if(rxState == ResultStatus::ready) {
			LL_SPI_DisableIT_ERR(spiHandle);
		}
		LL_SPI_DisableIT_TXE(spiHandle);

		txState = ResultStatus::ready;

		if(!isDummy) {
			CallInterrupt(Irq::Tx);
		}
	}



	// Stops both directions and clears their state. Interrupts are masked while the state changes, so an interrupt cannot
	// finish the transfer after the caller has aborted it.
	void StopAsyncState() {
		LL_SPI_DisableIT_TXE(spiHandle);
		LL_SPI_DisableIT_RXNE(spiHandle);
		LL_SPI_DisableIT_ERR(spiHandle);

		txState = ResultStatus::ready;
		txDataPointer = nullptr;
		txDataNeed = 0;
		txDataCounter = 0;

		rxState = ResultStatus::ready;
		rxDataPointer = nullptr;
		rxDataNeed = 0;
		rxDataCounter = 0;

		continuousAsyncRxMode = false;
	}



	// A frame on the wire finishes first, then its byte is dropped and the error flags are cleared, so the next transfer
	// does not start with a stale byte.
	void AbortAllTransfers() {
		uint32 primask = __get_PRIMASK();
		__disable_irq();
		StopAsyncState();
		__set_PRIMASK(primask);

		uint32 tickStart = System::GetTick();
		while(LL_SPI_IsActiveFlag_BSY(spiHandle)) {
			if((System::GetTick() - tickStart) > timeout) {
				break;
			}
		}

		if(LL_SPI_IsActiveFlag_RXNE(spiHandle)) {
			lastRxData = LL_SPI_ReceiveData8(spiHandle);
		}

		if(LL_SPI_IsActiveFlag_OVR(spiHandle)) {
			LL_SPI_ClearFlag_OVR(spiHandle);
		}

		ChipSelect(false);
	}



	constexpr uint32 CastTransferDirection() const {
		switch (parameters.direction) {
			case Direction::Tx: return LL_SPI_HALF_DUPLEX_TX;
			case Direction::Rx: return LL_SPI_HALF_DUPLEX_RX;
			case Direction::TxRx: return LL_SPI_FULL_DUPLEX;
		}
		SystemAbort();
		return 0;
	}


	constexpr uint32 CastMode() const {
		switch (parameters.mode) {
			case Mode::Master: return LL_SPI_MODE_MASTER;
			case Mode::Slave: return LL_SPI_MODE_SLAVE;
		}
		SystemAbort();
		return 0;
	}


	constexpr uint32 CastClockPolarity() const {
		switch (parameters.clockPolarity) {
			case ClockPolarity::High: return LL_SPI_POLARITY_HIGH;
			case ClockPolarity::Low: return LL_SPI_POLARITY_LOW;
		}
		SystemAbort();
		return 0;
	}


	constexpr uint32 CastClockPhase() const {
		switch (parameters.clockPhase) {
			case ClockPhase::Edge1: return LL_SPI_PHASE_1EDGE;
			case ClockPhase::Edge2: return LL_SPI_PHASE_2EDGE;
		}
		SystemAbort();
		return 0;
	}


	constexpr uint32 CastBitOrder() const {
		switch (parameters.firstBit) {
			case FirstBit::LSB: return LL_SPI_LSB_FIRST;
			case FirstBit::MSB: return LL_SPI_MSB_FIRST;
		}
		SystemAbort();
		return 0;
	}


};

