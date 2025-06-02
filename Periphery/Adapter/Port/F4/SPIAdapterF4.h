#pragma once
#include <System/System.h>
#include "../../SPIAdapter.h"



using ASPI = class SPIAdapterF4;


class SPIAdapterF4: public SPIAdapter {
private:
	struct BaudRatePrescaler {
		uint32 value;
		uint16 prescaler;
	};



public:
	SPIAdapterF4() { }

	SPIAdapterF4(SPI_TypeDef *spi, uint32 busClock = 0):SPIAdapter(spi, busClock) {

	}


	virtual void AbortReceive() override {

	}





	virtual void AbortTransmit() override {

	}




	virtual void IrqHandler() override {
		ReceiveInterrupt();
		TransmitInterrupt();
		ErrorInterrupt();
	}





private:
	inline void ErrorInterrupt() {
		if(LL_SPI_IsActiveFlag_MODF(spiHandle) && LL_SPI_IsEnabledIT_ERR(spiHandle)) {
			CallError(Error::ModeFault);
		}

		if(LL_SPI_IsActiveFlag_FRE(spiHandle) && LL_SPI_IsEnabledIT_ERR(spiHandle)) {
			CallError(Error::FrameFormat);
		}

		if(LL_SPI_IsActiveFlag_OVR(spiHandle) && LL_SPI_IsEnabledIT_ERR(spiHandle)) {
			CallError(Error::Overrun);
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

		if(txState == Status::ready) {
			LL_SPI_DisableIT_ERR(spiHandle);
		}
		LL_SPI_DisableIT_RXNE(spiHandle);

		rxState = Status::ready;

		CallInterrupt(Irq::Rx);
	}





	inline void TransmitInterrupt() {
		if (!LL_SPI_IsActiveFlag_TXE(spiHandle) && !LL_SPI_IsEnabledIT_TXE(spiHandle)) {
			return;
		}

		if(continuousAsyncTxMode) {
			abort(); // TODO: continuous
			return;
		}

		LL_SPI_TransmitData8(spiHandle, *(uint8*)txDataPointer++);

		if (++txDataCounter < txDataNeed) {
			return;
		}

		if(rxState == Status::ready) {
			LL_SPI_DisableIT_ERR(spiHandle);
		}
		LL_SPI_DisableIT_TXE(spiHandle);

		txState = Status::ready;

		CallInterrupt(Irq::Tx);
	}






protected:
	virtual Status::statusType Initialization() override {
		auto status = BeforeInitialization();
		if(status != Status::ok) {
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
			return Status::error;
		}

		LL_SPI_SetStandard(spiHandle, LL_SPI_PROTOCOL_MOTOROLA);

		return BeforeInitialization();
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





	virtual Status::statusType StartContinuousAsyncRxMode() {
		if (rxState != Status::ready) {
			return Status::busy;
		}

		rxState = Status::busy;
		txDataNeed = 0;
		txDataPointer = 0;
		txDataCounter = 0;

		LL_SPI_EnableIT_RXNE(spiHandle);
		LL_SPI_EnableIT_ERR(spiHandle);

		if(!LL_SPI_IsEnabled(spiHandle)) {
			LL_SPI_Enable(spiHandle);
		}

		return Status::ok;
	}





	virtual Status::statusType StopContinuousAsyncRxMode() {
		return Status::notSupported;
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


		if(!LL_SPI_IsEnabled(spiHandle)) {
			LL_SPI_Enable(spiHandle);
		}


		if (parameters.mode == Mode::Slave || size == 1) {
			LL_SPI_TransmitData8(spiHandle, *txDataPointer++);
			txDataCounter++;
		}


		for (; txDataCounter < txDataNeed; txDataCounter++) {
			while(!LL_SPI_IsActiveFlag_TXE(spiHandle)) {
				if((System::GetTick() - tickStart) > timeout) {
					LL_SPI_Disable(spiHandle);
					txState = Status::ready;
					return Status::timeout;
				}
			}

			LL_SPI_TransmitData8(spiHandle, *txDataPointer++);
		}


		if (parameters.direction == Direction::TxRx) {
			LL_SPI_ClearFlag_OVR(spiHandle);
		}

		LL_SPI_Disable(spiHandle);
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


		if(!LL_SPI_IsEnabled(spiHandle)) {
			LL_SPI_Enable(spiHandle);
		}


		for (rxDataCounter = 0; rxDataCounter < rxDataNeed; rxDataCounter++) {
			while(!LL_SPI_IsActiveFlag_RXNE(spiHandle)) {
				if((System::GetTick() - tickStart) > timeout) {
					LL_SPI_Disable(spiHandle);
					rxState = Status::ready;
					return Status::timeout;
				}
			}

			lastRxData = LL_SPI_ReceiveData8(spiHandle);
			*rxDataPointer++ = lastRxData;
		}


		LL_SPI_Disable(spiHandle);
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


		if(!LL_SPI_IsEnabled(spiHandle)) {
			LL_SPI_Enable(spiHandle);
		}


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
				LL_SPI_Disable(spiHandle);
				txState = Status::ready;
				rxState = Status::ready;
				return Status::timeout;
			}
		}


		if (parameters.direction == Direction::TxRx) {
			LL_SPI_ClearFlag_OVR(spiHandle);
		}

		LL_SPI_Disable(spiHandle);

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

		LL_SPI_EnableIT_TXE(spiHandle);
		LL_SPI_EnableIT_ERR(spiHandle);

		if(!LL_SPI_IsEnabled(spiHandle)) {
			LL_SPI_Enable(spiHandle);
		}

		return Status::ok;
	}





	inline virtual Status::statusType ReadByteArrayAsync(uint8 *buffer, uint32 size) override {
		if (rxState != Status::ready) {
			return Status::busy;
		}

		rxState = Status::busy;
		txDataNeed = size;
		txDataPointer = buffer;
		txDataCounter = 0;

		LL_SPI_EnableIT_RXNE(spiHandle);
		LL_SPI_EnableIT_ERR(spiHandle); // TODO:SetContinuousAsyncRxMode

		if(!LL_SPI_IsEnabled(spiHandle)) { // TODO:SetContinuousAsyncRxMode
			LL_SPI_Enable(spiHandle);
		}

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

		LL_SPI_EnableIT_TXE(spiHandle);
		LL_SPI_EnableIT_RXNE(spiHandle);
		LL_SPI_EnableIT_ERR(spiHandle); // TODO:SetContinuousAsyncRxMode

		if(!LL_SPI_IsEnabled(spiHandle)) { // TODO:SetContinuousAsyncRxMode
			LL_SPI_Enable(spiHandle);
		}

		return Status::ok;
	}





private:
	constexpr uint32 CastTransferDirection() const {
		switch (parameters.direction) {
			case Direction::Tx: return LL_SPI_HALF_DUPLEX_TX;
			case Direction::Rx: return LL_SPI_HALF_DUPLEX_RX;
			case Direction::TxRx: return LL_SPI_FULL_DUPLEX;
		}
		abort();
		return 0;
	}


	constexpr uint32 CastMode() const {
		switch (parameters.mode) {
			case Mode::Master: return LL_SPI_MODE_MASTER;
			case Mode::Slave: return LL_SPI_MODE_SLAVE;
		}
		abort();
		return 0;
	}


	constexpr uint32 CastClockPolarity() const {
		switch (parameters.clockPolarity) {
			case ClockPolarity::High: return LL_SPI_POLARITY_HIGH;
			case ClockPolarity::Low: return LL_SPI_POLARITY_LOW;
		}
		abort();
		return 0;
	}


	constexpr uint32 CastClockPhase() const {
		switch (parameters.clockPhase) {
			case ClockPhase::Edge1: return LL_SPI_PHASE_1EDGE;
			case ClockPhase::Edge2: return LL_SPI_PHASE_2EDGE;
		}
		abort();
		return 0;
	}


	constexpr uint32 CastBitOrder() const {
		switch (parameters.firstBit) {
			case FirstBit::LSB: return LL_SPI_LSB_FIRST;
			case FirstBit::MSB: return LL_SPI_MSB_FIRST;
		}
		abort();
		return 0;
	}


};

