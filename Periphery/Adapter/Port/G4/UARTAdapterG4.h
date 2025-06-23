#pragma once
#include "../../UARTAdapter.h"
#include <System/System.h>
#include <cassert>




using AUART = class UARTAdapterG4;


class UARTAdapterG4: public UARTAdapter {
public:
	UARTAdapterG4() { }

	UARTAdapterG4(USART_TypeDef *uart):UARTAdapter(uart) {

	}





	virtual void AbortReceive() override {
		LL_USART_DisableIT_RXNE(uartHandle);
		LL_USART_DisableIT_PE(uartHandle);
		LL_USART_DisableIT_ERROR(uartHandle);

		continuousAsyncRxMode = false;
		rxDataCounter = 0;
		rxDataNeed = 0;
		rxDataPointer = nullptr;
		rxState = Status::ready;
	}





	virtual void AbortTransmit() override {
		LL_USART_DisableIT_TXE(uartHandle);
		LL_USART_DisableIT_TC(uartHandle);

		txDataCounter = 0;
		txDataNeed = 0;
		txDataPointer = nullptr;
		txState = Status::ready;
	}





	virtual void IrqHandler() override {
		ErrorInterrupt();
		ReceiveInterrupt();
		TransmitInterrupt();
		EndTransmitInterrupt();
	}








protected:
	virtual Status::statusType Initialization() override {
		auto status = BeforeInitialization();
		if(status != Status::ok) {
			return status;
		}

		LL_USART_InitTypeDef init = {
			.PrescalerValue = LL_USART_PRESCALER_DIV1,
			.BaudRate = parameters.baudRate,
			.DataWidth = LL_USART_DATAWIDTH_8B,
			.StopBits = CastStopBits(),
			.Parity = CastParity(),
			.TransferDirection = CastMode(),
			.HardwareFlowControl = CastFlowControl(),
			.OverSampling = CastOverSampling()
		};

		SystemAssert(LL_USART_Init(uartHandle, &init) == ErrorStatus::SUCCESS);

		LL_USART_SetTXFIFOThreshold(uartHandle, LL_USART_FIFOTHRESHOLD_1_8);
		LL_USART_SetRXFIFOThreshold(uartHandle, LL_USART_FIFOTHRESHOLD_1_8);
		LL_USART_DisableFIFO(uartHandle);
		LL_USART_ConfigAsyncMode(uartHandle);

		LL_USART_Enable(uartHandle);

		// Polling USART initialisation
		while((!(LL_USART_IsActiveFlag_TEACK(uartHandle))) || (!(LL_USART_IsActiveFlag_REACK(uartHandle))));

		return AfterInitialization();
	}





	virtual inline Status::statusType WriteByteArray(uint8* buffer, uint32 size) override {
		if (txState != Status::ready) {
			return Status::busy;
		}

		txState = Status::busy;
		txDataNeed = size;
		txDataPointer = buffer;
		uint32 tickStart = System::GetTick();

		for (txDataCounter = 0; txDataCounter < txDataNeed; txDataCounter++) {
			while(!LL_USART_IsActiveFlag_TXE(uartHandle)) {
				if((System::GetTick() - tickStart) > timeout) {
					txState = Status::ready;
					return Status::timeout;
				}
			}

			LL_USART_TransmitData8(uartHandle, *(uint8*)txDataPointer++);
		}

		while(!LL_USART_IsActiveFlag_TC(uartHandle)) {
			if((System::GetTick() - tickStart) > timeout) {
				txState = Status::ready;
				return Status::timeout;
			}
		}

		txState = Status::ready;
		return Status::ok;
	}





	virtual inline Status::statusType ReadByteArray(uint8 *buffer, uint32 size) override {
		if (rxState != Status::ready) {
			return Status::busy;
		}

		rxState = Status::busy;
		rxDataNeed = size;
		rxDataPointer = buffer;
		uint32 tickStart = System::GetTick();

		for (rxDataCounter = 0; rxDataCounter < rxDataNeed; rxDataCounter++) {
			while(!LL_USART_IsActiveFlag_RXNE(uartHandle)) {
				if((System::GetTick() - tickStart) > timeout) {
					rxState = Status::ready;
					return Status::timeout;
				}
			}

			uint8 mask = parameters.parity == Parity::None ? 0xFF : 0x7F;
			lastRxData = LL_USART_ReceiveData8(uartHandle) & mask;
			*rxDataPointer++ = lastRxData;
		}

		rxState = Status::ready;
		return Status::ok;
	}





	virtual inline Status::statusType WriteByteArrayAsync(uint8* buffer, uint32 size) override {
		if (txState != Status::ready) {
			return Status::busy;
		}

		txState = Status::busy;
		txDataPointer = buffer;
		txDataNeed = size;
		txDataCounter = 0;

		LL_USART_EnableIT_TXE(uartHandle);

		return Status::ok;
	}





	virtual inline Status::statusType ReadByteArrayAsync(uint8* buffer, uint32 size) override {
		if (rxState != Status::ready) {
			return Status::busy;
		}

		rxState = Status::busy;
		rxDataPointer = buffer;
		rxDataNeed = size;
		rxDataCounter = 0;

		LL_USART_EnableIT_PE(uartHandle);
		LL_USART_EnableIT_RXNE(uartHandle);
		LL_USART_EnableIT_ERROR(uartHandle);

		return Status::ok;
	}





	virtual Status::statusType StartContinuousAsyncRxMode() override {
		if(continuousAsyncRxMode) {
			return Status::notAvailable;
		}

		if (rxState != Status::ready) {
			return Status::busy;
		}

		rxState = Status::busy;

		LL_USART_EnableIT_PE(uartHandle);
		LL_USART_EnableIT_RXNE(uartHandle);
		LL_USART_EnableIT_ERROR(uartHandle);

		return Status::ok;
	}





	virtual Status::statusType StopContinuousAsyncRxMode() override {
		if(!continuousAsyncRxMode) {
			return Status::notAvailable;
		}

		LL_USART_DisableIT_PE(uartHandle);
		LL_USART_DisableIT_RXNE(uartHandle);
		LL_USART_DisableIT_ERROR(uartHandle);
		rxState = Status::ready;

		return Status::ok;
	}





private:
	inline void ErrorInterrupt() {
		if(LL_USART_IsActiveFlag_PE(uartHandle) && LL_USART_IsEnabledIT_PE(uartHandle)) {
			CallError(Error::Parity);
		}

		if(LL_USART_IsActiveFlag_FE(uartHandle) && LL_USART_IsEnabledIT_ERROR(uartHandle)) {
			CallError(Error::Frame);
		}

		if(LL_USART_IsActiveFlag_NE(uartHandle) && LL_USART_IsEnabledIT_ERROR(uartHandle)) {
			CallError(Error::Noise);
		}

		if(LL_USART_IsActiveFlag_ORE(uartHandle) && (LL_USART_IsEnabledIT_ERROR(uartHandle) || LL_USART_IsEnabledIT_RXNE(uartHandle))) {
			CallError(Error::Overrun);
		}
	}





	inline void ReceiveInterrupt() {
		if(!LL_USART_IsActiveFlag_RXNE(uartHandle) || !LL_USART_IsEnabledIT_RXNE(uartHandle)) {
			return;
		}

		uint8 mask = parameters.parity == Parity::None ? 0xFF : 0x7F;
		lastRxData = LL_USART_ReceiveData8(uartHandle) & mask;

		if(continuousAsyncRxMode) {
			CallInterrupt(Irq::Rx);
			return;
		}

		*rxDataPointer++ = lastRxData;

		if (++rxDataCounter < rxDataNeed) {
			return;
		}

		LL_USART_DisableIT_PE(uartHandle);
		LL_USART_DisableIT_RXNE(uartHandle);
		LL_USART_DisableIT_ERROR(uartHandle);

		rxState = Status::ready;

		CallInterrupt(Irq::Rx);
	}





	inline void TransmitInterrupt() {
		if(!LL_USART_IsActiveFlag_TXE(uartHandle) || !LL_USART_IsEnabledIT_TXE(uartHandle)) {
			return;
		}

		if(continuousAsyncTxMode) {
			abort(); // TODO: [VHAL] [UART] [G4] [ADD SUPPORT] continuous
			return;
		}

		LL_USART_TransmitData8(uartHandle, *(uint8*)txDataPointer++);

		if (++txDataCounter < txDataNeed) {
			return;
		}

		LL_USART_DisableIT_TXE(uartHandle);
		LL_USART_EnableIT_TC(uartHandle);
	}





	inline void EndTransmitInterrupt() {
		if(!LL_USART_IsActiveFlag_TC(uartHandle) || !LL_USART_IsEnabledIT_TC(uartHandle)) {
			return;
		}

		LL_USART_ClearFlag_TC(uartHandle);
		LL_USART_DisableIT_TC(uartHandle);

		txState = Status::ready;

		CallInterrupt(Irq::Tx);
	}







private:
	constexpr uint32 CastStopBits() const {
		switch (parameters.stopBits) {
			case StopBits::B1: return LL_USART_STOPBITS_1;
			case StopBits::B2: return LL_USART_STOPBITS_2;
			default:
				abort();
				return 0;
			break;
		}
	}



	constexpr uint32 CastParity() const {
		switch (parameters.parity) {
			case Parity::None: return LL_USART_PARITY_NONE;
			case Parity::Even: return LL_USART_PARITY_EVEN;
			case Parity::Odd: return LL_USART_PARITY_ODD;
			default:
				abort();
				return 0;
			break;
		}
	}



	constexpr uint32 CastMode() const {
		switch (parameters.mode) {
			case Mode::Tx: return LL_USART_DIRECTION_TX;
			case Mode::Rx: return LL_USART_DIRECTION_RX;
			case Mode::TxRx: return LL_USART_DIRECTION_TX_RX;
			default:
				abort();
				return 0;
			break;
		}
	}



	constexpr uint32 CastFlowControl() const {
		switch (parameters.flowControl) {
			case FlowControl::None: return LL_USART_HWCONTROL_NONE;
			case FlowControl::Rts: return LL_USART_HWCONTROL_RTS;
			case FlowControl::Cts: return LL_USART_HWCONTROL_CTS;
			case FlowControl::RtsCts: return LL_USART_HWCONTROL_RTS_CTS;
			default:
				abort();
				return 0;
			break;
		}
	}



	constexpr uint32 CastOverSampling() const {
		switch (parameters.overSampling) {
			case OverSampling::B16: return LL_USART_OVERSAMPLING_16;
			case OverSampling::B8: return LL_USART_OVERSAMPLING_8;
			default:
				abort();
				return 0;
			break;
		}
	}

};

