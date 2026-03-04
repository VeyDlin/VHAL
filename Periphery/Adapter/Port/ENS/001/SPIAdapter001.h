#pragma once
#include <System/System.h>
#include <Adapter/SPIAdapter.h>


using ASPI = class SPIAdapter001;

class SPIAdapter001: public SPIAdapter<CMSDK_SPI_TypeDef> {
public:
	SPIAdapter001() { }

	SPIAdapter001(CMSDK_SPI_TypeDef *spi, uint32 busClockHz = 0):SPIAdapter(spi, busClockHz) {

	}


	virtual void AbortReceive() override {
		// Disable RX interrupts (IER)
		spiHandle->IER &= ~(1 << 0);

		// Reset RX FIFO via FCR
		spiHandle->FCR |= (1 << 1);

		continuousAsyncRxMode = false;
		rxDataCounter = 0;
		rxDataNeed = 0;
		rxDataPointer = nullptr;
		rxState = Status::ready;
	}





	virtual void AbortTransmit() override {
		// Disable TX interrupts (IER)
		spiHandle->IER &= ~(1 << 1);

		// Reset TX FIFO via FCR (TX_FIFO_CLR = bit 8)
		spiHandle->FCR |= (1 << 8);

		txDataCounter = 0;
		txDataNeed = 0;
		txDataPointer = nullptr;
		txState = Status::ready;
	}




	virtual void IrqHandler() override {
		ReceiveInterrupt();
		TransmitInterrupt();
		ErrorInterrupt();
	}





private:
	inline void ErrorInterrupt() {
		uint32 intStatus = spiHandle->INTSTATUS;

		// Overrun error (bit 3 = OVERRUN_INT_STS)
		if(intStatus & (1 << 3)) {
			spiHandle->INTCLEAR = (1 << 3);
			CallError(Error::Overrun);
		}

		// Underrun error (bit 4 = UNDERRUN_INT_STS)
		if(intStatus & (1 << 4)) {
			spiHandle->INTCLEAR = (1 << 4);
			CallError(Error::ModeFault);
		}
	}





	inline void ReceiveInterrupt() {
		uint32 intStatus = spiHandle->INTSTATUS;

		// Check RX interrupt flag
		if(!(intStatus & (1 << 0))) {
			return;
		}

		lastRxData = spiHandle->RBR;
		spiHandle->INTCLEAR = (1 << 0);

		if(continuousAsyncRxMode) {
			CallInterrupt(Irq::Rx);
			return;
		}

		*rxDataPointer++ = lastRxData;

		if (++rxDataCounter < rxDataNeed) {
			return;
		}

		if(txState == Status::ready) {
			// Disable error interrupts (OVERRUN + UNDERRUN)
			spiHandle->IER &= ~((1 << 3) | (1 << 4));
		}
		// Disable RX interrupt
		spiHandle->IER &= ~(1 << 0);

		rxState = Status::ready;

		CallInterrupt(Irq::Rx);
	}





	inline void TransmitInterrupt() {
		uint32 intStatus = spiHandle->INTSTATUS;

		// Check TX interrupt flag
		if(!(intStatus & (1 << 1))) {
			return;
		}

		if(continuousAsyncTxMode) {
			abort(); // TODO: continuous
			return;
		}

		spiHandle->THR = *(uint8*)txDataPointer++;
		spiHandle->INTCLEAR = (1 << 1);

		if (++txDataCounter < txDataNeed) {
			return;
		}

		if(rxState == Status::ready) {
			// Disable error interrupts (OVERRUN + UNDERRUN)
			spiHandle->IER &= ~((1 << 3) | (1 << 4));
		}
		// Disable TX interrupt
		spiHandle->IER &= ~(1 << 1);

		txState = Status::ready;

		CallInterrupt(Irq::Tx);
	}






protected:
	virtual Status::statusType Initialization() override {
		OnEnableClock();

		auto status = BeforeInitialization();
		if(status != Status::ok) {
			return status;
		}

		// Wait for SPI not busy before disabling (FSR bit 4)
		while(spiHandle->FSR & (1 << 4)) { }

		// Disable SPI before configuration
		spiHandle->CTRL1 &= ~(1 << 0);

		// Configure CTRL1
		uint32 ctrl1 = 0;

		// Master/Slave selection (bit 1)
		if(parameters.mode == Mode::Master) {
			ctrl1 |= (1 << 1);
		}

		// CPHA (bit 2)
		if(parameters.clockPhase == ClockPhase::Edge2) {
			ctrl1 |= (1 << 2);
		}

		// CPOL (bit 3)
		if(parameters.clockPolarity == ClockPolarity::High) {
			ctrl1 |= (1 << 3);
		}

		// Baud rate prescaler (bits [6:4])
		ctrl1 |= (baudRatePrescaler & 0x7) << 4;

		// LSB first (bit 7)
		if(parameters.firstBit == FirstBit::LSB) {
			ctrl1 |= (1 << 7);
		}

		// NSS_TOGGLE (bit 11) - required for proper chip select pulsing
		ctrl1 |= (1 << 11);

		// Transmission mode (bits [15:12])
		// 0x0=full-duplex, 0x1=TX-only, 0x2=RX-only
		switch(parameters.direction) {
			case Direction::Tx:
				ctrl1 |= (0x1 << 12);
			break;

			case Direction::Rx:
				ctrl1 |= (0x2 << 12);
			break;

			case Direction::TxRx:
			default:
				// 0x0 = full duplex, already zero
			break;
		}

		spiHandle->CTRL1 = ctrl1;

		// Configure CTRL2: 8-bit character length (bits [3:0] = 0x7 for 8-bit)
		uint32 ctrl2 = spiHandle->CTRL2;
		ctrl2 &= ~0x0F;
		ctrl2 |= 0x07;		// 8-bit character length (value 7 = 8 bits)

		// Enable hardware NSS0 (bit 8) for master mode
		if (parameters.mode == Mode::Master) {
			ctrl2 |= (1 << 8); // NSS0_EN
		}

		spiHandle->CTRL2 = ctrl2;

		// Enable FIFO, reset RX FIFO (bit 1) and TX FIFO (bit 8)
		spiHandle->FCR = (1 << 0) | (1 << 1) | (1 << 8);

		// Enable SPI (bit 0 of CTRL1)
		spiHandle->CTRL1 |= (1 << 0);

		return AfterInitialization();
	}





	uint32 CalculatePrescaler() override {
		// BAUD_RATE field = 0..7, actual divider = 2^(BAUD_RATE+1)
		// i.e. 0 -> /2, 1 -> /4, 2 -> /8, ... 7 -> /256

		if(parameters.maxSpeedHz == 0 || inputBusClockHz == 0) {
			baudRatePrescaler = 7;
			return inputBusClockHz / 256;
		}

		for (uint8 i = 0; i < 8; i++) {
			uint32 divider = 1 << (i + 1);		// 2, 4, 8, 16, 32, 64, 128, 256
			uint32 clock = inputBusClockHz / divider;

			if(clock <= parameters.maxSpeedHz) {
				baudRatePrescaler = i;
				return clock;
			}
		}

		baudRatePrescaler = 7;
		return inputBusClockHz / 256;
	}





	virtual Status::statusType StartContinuousAsyncRxMode() override {
		if (rxState != Status::ready) {
			return Status::busy;
		}

		rxState = Status::busy;
		continuousAsyncRxMode = true;
		rxDataNeed = 0;
		rxDataPointer = 0;
		rxDataCounter = 0;

		// Enable RX and error interrupts (OVERRUN + UNDERRUN)
		spiHandle->IER |= (1 << 0) | (1 << 3) | (1 << 4);

		return Status::ok;
	}





	virtual Status::statusType StopContinuousAsyncRxMode() override {
		if(!continuousAsyncRxMode) {
			return Status::notAvailable;
		}

		// Disable RX and error interrupts
		spiHandle->IER &= ~((1 << 0) | (1 << 3) | (1 << 4));

		continuousAsyncRxMode = false;
		rxState = Status::ready;

		return Status::ok;
	}





	inline virtual Status::statusType WriteByteArray(uint8 *buffer, uint32 size) override {
		if (txState != Status::ready) {
			return Status::busy;
		}

		txState = Status::busy;
		txDataNeed = size;
		txDataPointer = buffer;
		txDataCounter = 0;
		uint32 tickStart = System::GetTick();


		if (parameters.mode == Mode::Slave || size == 1) {
			spiHandle->THR = *txDataPointer++;
			txDataCounter++;
		}


		for (; txDataCounter < txDataNeed; txDataCounter++) {
			// Wait for TX FIFO empty (FSR bit 0)
			while(!(spiHandle->FSR & (1 << 0))) {
				if((System::GetTick() - tickStart) > timeout) {
					txState = Status::ready;
					return Status::timeout;
				}
			}

			spiHandle->THR = *txDataPointer++;
		}

		// Wait for SPI not busy (FSR bit 4)
		while(spiHandle->FSR & (1 << 4)) {
			if((System::GetTick() - tickStart) > timeout) {
				txState = Status::ready;
				return Status::timeout;
			}
		}

		// Flush any received data in full-duplex mode
		if (parameters.direction == Direction::TxRx) {
			volatile uint32 dummy;
			while((spiHandle->FSR >> 16) & 0x1F) {	// While RX FIFO not empty (RX_FIFO_LEN bits [20:16])
				dummy = spiHandle->RBR;
				(void)dummy;
			}
		}

		txState = Status::ready;

		return Status::ok;
	}





	inline virtual Status::statusType ReadByteArray(uint8 *buffer, uint32 size) override {
		if (rxState != Status::ready) {
			return Status::busy;
		}

		rxState = Status::busy;
		rxDataNeed = size;
		rxDataPointer = buffer;
		uint32 tickStart = System::GetTick();


		for (rxDataCounter = 0; rxDataCounter < rxDataNeed; rxDataCounter++) {
			// Send dummy byte to generate clock
			while(!(spiHandle->FSR & (1 << 0))) {
				if((System::GetTick() - tickStart) > timeout) {
					rxState = Status::ready;
					return Status::timeout;
				}
			}
			spiHandle->THR = 0xFF;

			// Wait for SPI not busy (FSR bit 4)
			while(spiHandle->FSR & (1 << 4)) {
				if((System::GetTick() - tickStart) > timeout) {
					rxState = Status::ready;
					return Status::timeout;
				}
			}

			lastRxData = spiHandle->RBR;
			*rxDataPointer++ = lastRxData;
		}


		rxState = Status::ready;

		return Status::ok;
	}





	inline virtual Status::statusType WriteReadByteArray(uint8 *txBuffer, uint8 *rxBuffer, uint32 size) override {
		if (txState != Status::ready || rxState != Status::ready) {
			return Status::busy;
		}

		txState = Status::busy;
		txDataNeed = size;
		txDataPointer = txBuffer;
		txDataCounter = 0;

		rxState = Status::busy;
		rxDataNeed = size;
		rxDataPointer = rxBuffer;
		rxDataCounter = 0;
		uint32 tickStart = System::GetTick();


		if (parameters.mode == Mode::Slave || size == 1) {
			spiHandle->THR = *txDataPointer++;
			txDataCounter++;
		}


		bool isTxAllowed = true;

		while (txDataCounter < txDataNeed || rxDataCounter < rxDataNeed) {
			// TX: check TX FIFO empty (FSR bit 0)
			if ((spiHandle->FSR & (1 << 0)) && txDataCounter < txDataNeed && isTxAllowed) {
				spiHandle->THR = *txDataPointer++;
				txDataCounter++;
				isTxAllowed = false;
			}

			// RX: check RX FIFO not empty (RX_FIFO_LEN bits [20:16])
			if (((spiHandle->FSR >> 16) & 0x1F) && rxDataCounter < rxDataNeed) {
				lastRxData = spiHandle->RBR;
				*rxDataPointer++ = lastRxData;
				rxDataCounter++;
				isTxAllowed = true;
			}

			if((System::GetTick() - tickStart) > timeout) {
				txState = Status::ready;
				rxState = Status::ready;
				return Status::timeout;
			}
		}


		txState = Status::ready;
		rxState = Status::ready;

		return Status::ok;
	}





	inline virtual Status::statusType WriteByteArrayAsync(uint8 *buffer, uint32 size) override {
		if (txState != Status::ready) {
			return Status::busy;
		}

		txState = Status::busy;
		txDataNeed = size;
		txDataPointer = buffer;
		txDataCounter = 0;

		// Enable TX and error interrupts (OVERRUN + UNDERRUN)
		spiHandle->IER |= (1 << 1) | (1 << 3) | (1 << 4);

		return Status::ok;
	}





	inline virtual Status::statusType ReadByteArrayAsync(uint8 *buffer, uint32 size) override {
		if (rxState != Status::ready) {
			return Status::busy;
		}

		rxState = Status::busy;
		rxDataNeed = size;
		rxDataPointer = buffer;
		rxDataCounter = 0;

		// Enable RX and error interrupts (OVERRUN + UNDERRUN)
		spiHandle->IER |= (1 << 0) | (1 << 3) | (1 << 4);

		return Status::ok;
	}





	inline virtual Status::statusType WriteReadByteArrayAsync(uint8 *txBuffer, uint8 *rxBuffer, uint32 size) override {
		if (txState != Status::ready || rxState != Status::ready) {
			return Status::busy;
		}

		txState = Status::busy;
		txDataNeed = size;
		txDataPointer = txBuffer;
		txDataCounter = 0;

		rxState = Status::busy;
		rxDataNeed = size;
		rxDataPointer = rxBuffer;
		rxDataCounter = 0;

		// Enable TX, RX, and error interrupts (OVERRUN + UNDERRUN)
		spiHandle->IER |= (1 << 0) | (1 << 1) | (1 << 3) | (1 << 4);

		return Status::ok;
	}





private:
	inline void OnEnableClock() {
		// Enable SPI clock via APB clock enable register
#ifdef CMSDK_SPI0
		if(spiHandle == CMSDK_SPI0) {
			CMSDK_SYSCON->APB_CLKEN |= (1 << 2);
			return;
		}
#endif

#ifdef CMSDK_SPI1
		if(spiHandle == CMSDK_SPI1) {
			CMSDK_SYSCON->APB_CLKEN |= (1 << 3);
			return;
		}
#endif
	}
};
