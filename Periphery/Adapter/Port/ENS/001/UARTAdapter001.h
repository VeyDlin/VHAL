#pragma once
#include <Adapter/UARTAdapter.h>


using AUART = class UARTAdapter001;

class UARTAdapter001: public UARTAdapter<CMSDK_UART_TypeDef> {
private:
	uint32 inputBusClockHz = 0;

public:
	UARTAdapter001() { }

	UARTAdapter001(CMSDK_UART_TypeDef *uart, uint32 busClockHz):UARTAdapter(uart), inputBusClockHz(busClockHz) {

	}





	virtual void AbortReceive() override {
		uartHandle->IER &= ~(1 << 0);	// Disable RX data available interrupt
		uartHandle->IER &= ~(1 << 2);	// Disable RX line status interrupt

		continuousAsyncRxMode = false;
		rxDataCounter = 0;
		rxDataNeed = 0;
		rxDataPointer = nullptr;
		rxState = ResultStatus::ready;
	}





	virtual void AbortTransmit() override {
		uartHandle->IER &= ~(1 << 1);	// Disable THR empty interrupt

		txDataCounter = 0;
		txDataNeed = 0;
		txDataPointer = nullptr;
		txState = ResultStatus::ready;
	}





	virtual void IrqHandler() override {
		ErrorInterrupt();
		ReceiveInterrupt();
		TransmitInterrupt();
	}







protected:
	virtual ResultStatus Initialization() override {
		OnEnableClock();

		auto status = BeforeInitialization();
		if(status != ResultStatus::ok) {
			return status;
		}

		// Calculate baud rate divisor accounting for oversampling mode
		uint32 pclk = inputBusClockHz;

		// Check oversampling mode from MDR
		uint32 divisorMultiplier = (uartHandle->MDR & 0x01) ? 13 : 16;
		uint32 divisor = pclk / (divisorMultiplier * parameters.baudRate);
		uartHandle->DLL = divisor & 0xFF;
		uartHandle->DLH = (divisor >> 8) & 0xFF;

		// Configure LCR: word length, stop bits, parity
		uint32 lcr = 0;

		// Word length <1:0>: 0=5bit, 1=6bit, 2=7bit, 3=8bit
		lcr |= 0x03; // 8-bit (base class Parameters has no wordLength field)

		// Stop bits
		if(parameters.stopBits == StopBits::B2) {
			lcr |= (1 << 2);
		}

		// Parity
		if(parameters.parity != Parity::None) {
			lcr |= (1 << 3);	// Parity enable
			if(parameters.parity == Parity::Even) {
				lcr |= (1 << 4);	// Even parity
			}
		}

		uartHandle->LCR = lcr;

		// Enable FIFO, reset TX and RX FIFOs
		uartHandle->FCR = (1 << 0) | (1 << 1) | (1 << 2);

		return AfterInitialization();
	}





	virtual inline ResultStatus WriteByteArray(uint8* buffer, uint32 size) override {
		if (txState != ResultStatus::ready) {
			return ResultStatus::busy;
		}

		txState = ResultStatus::busy;
		txDataNeed = size;
		txDataPointer = buffer;
		uint32 tickStart = System::GetTick();

		for (txDataCounter = 0; txDataCounter < txDataNeed; txDataCounter++) {
			// Wait for THR empty (LSR bit 5)
			while(!(uartHandle->LSR & (1 << 5))) {
				if((System::GetTick() - tickStart) > timeout) {
					txState = ResultStatus::ready;
					return ResultStatus::timeout;
				}
			}

			uartHandle->THR = *(uint8*)txDataPointer++;
		}

		// Wait for transmitter empty (LSR bit 6)
		while(!(uartHandle->LSR & (1 << 6))) {
			if((System::GetTick() - tickStart) > timeout) {
				txState = ResultStatus::ready;
				return ResultStatus::timeout;
			}
		}

		txState = ResultStatus::ready;
		return ResultStatus::ok;
	}





	virtual inline ResultStatus ReadByteArray(uint8 *buffer, uint32 size) override {
		if (rxState != ResultStatus::ready) {
			return ResultStatus::busy;
		}

		rxState = ResultStatus::busy;
		rxDataNeed = size;
		rxDataPointer = buffer;
		uint32 tickStart = System::GetTick();

		for (rxDataCounter = 0; rxDataCounter < rxDataNeed; rxDataCounter++) {
			// Wait for data ready (LSR bit 0)
			while(!(uartHandle->LSR & (1 << 0))) {
				if((System::GetTick() - tickStart) > timeout) {
					rxState = ResultStatus::ready;
					return ResultStatus::timeout;
				}
			}

			uint8 mask = parameters.parity == Parity::None ? 0xFF : 0x7F;
			lastRxData = uartHandle->RBR & mask;
			*rxDataPointer++ = lastRxData;
		}

		rxState = ResultStatus::ready;
		return ResultStatus::ok;
	}





	virtual inline ResultStatus WriteByteArrayAsync(uint8* buffer, uint32 size) override {
		if (txState != ResultStatus::ready) {
			return ResultStatus::busy;
		}

		txState = ResultStatus::busy;
		txDataPointer = buffer;
		txDataNeed = size;
		txDataCounter = 0;

		// Enable THR empty interrupt (IER bit 1)
		uartHandle->IER |= (1 << 1);

		return ResultStatus::ok;
	}





	virtual inline ResultStatus ReadByteArrayAsync(uint8* buffer, uint32 size) override {
		if (rxState != ResultStatus::ready) {
			return ResultStatus::busy;
		}

		rxState = ResultStatus::busy;
		rxDataPointer = buffer;
		rxDataNeed = size;
		rxDataCounter = 0;

		// Enable RX data available interrupt (IER bit 0) and RX line status interrupt (IER bit 2)
		uartHandle->IER |= (1 << 0) | (1 << 2);

		return ResultStatus::ok;
	}





	virtual ResultStatus StartContinuousAsyncRxMode() override {
		if(continuousAsyncRxMode) {
			return ResultStatus::notAvailable;
		}

		if (rxState != ResultStatus::ready) {
			return ResultStatus::busy;
		}

		rxState = ResultStatus::busy;
		continuousAsyncRxMode = true;

		// Enable RX data available interrupt (IER bit 0) and RX line status interrupt (IER bit 2)
		uartHandle->IER |= (1 << 0) | (1 << 2);

		return ResultStatus::ok;
	}





	virtual ResultStatus StopContinuousAsyncRxMode() override {
		if(!continuousAsyncRxMode) {
			return ResultStatus::notAvailable;
		}

		// Disable RX data available interrupt (IER bit 0) and RX line status interrupt (IER bit 2)
		uartHandle->IER &= ~((1 << 0) | (1 << 2));
		rxState = ResultStatus::ready;

		return ResultStatus::ok;
	}





private:
	inline void OnEnableClock() {
		// Enable UART clock via APB clock enable register
		// UART0 and UART1 are on APB bus
#ifdef CMSDK_UART0
		if(uartHandle == CMSDK_UART0) {
			CMSDK_SYSCON->APB_CLKEN |= (1 << 0);
			return;
		}
#endif

#ifdef CMSDK_UART1
		if(uartHandle == CMSDK_UART1) {
			CMSDK_SYSCON->APB_CLKEN |= (1 << 1);
			return;
		}
#endif
	}



	inline void ErrorInterrupt() {
		// Only process errors if RX line status interrupt is enabled (IER bit 2)
		if (!(uartHandle->IER & (1 << 2))) {
			return;
		}

		uint32 lsr = uartHandle->LSR;

		// Parity error (LSR bit 2)
		if(lsr & (1 << 2)) {
			CallError(Error::Parity);
		}

		// Framing error (LSR bit 3)
		if(lsr & (1 << 3)) {
			CallError(Error::Frame);
		}

		// Overrun error (LSR bit 1)
		if(lsr & (1 << 1)) {
			CallError(Error::Overrun);
		}
	}





	inline void ReceiveInterrupt() {
		// Check if RX data available interrupt is enabled (IER bit 0) and data ready (LSR bit 0)
		if(!(uartHandle->IER & (1 << 0)) || !(uartHandle->LSR & (1 << 0))) {
			return;
		}

		uint8 mask = parameters.parity == Parity::None ? 0xFF : 0x7F;
		lastRxData = uartHandle->RBR & mask;

		if(continuousAsyncRxMode) {
			CallInterrupt(Irq::Rx);
			return;
		}

		*rxDataPointer++ = lastRxData;

		if (++rxDataCounter < rxDataNeed) {
			return;
		}

		// Disable RX data available interrupt (IER bit 0) and RX line status interrupt (IER bit 2)
		uartHandle->IER &= ~((1 << 0) | (1 << 2));

		rxState = ResultStatus::ready;

		CallInterrupt(Irq::Rx);
	}





	inline void TransmitInterrupt() {
		// Check if THR empty interrupt is enabled (IER bit 1) and THR empty (LSR bit 5)
		if(!(uartHandle->IER & (1 << 1)) || !(uartHandle->LSR & (1 << 5))) {
			return;
		}

		if(continuousAsyncTxMode) {
			SystemAbort(); // TODO: continuous
			return;
		}

		uartHandle->THR = *(uint8*)txDataPointer++;

		if (++txDataCounter < txDataNeed) {
			return;
		}

		// Disable THR empty interrupt (IER bit 1)
		uartHandle->IER &= ~(1 << 1);

		txState = ResultStatus::ready;

		CallInterrupt(Irq::Tx);
	}
};
