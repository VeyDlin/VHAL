#pragma once
#include <Adapter/DMAAdapter.h>

using ADMA = class DMAAdapterG0;


class DMAAdapterG0: public DMAAdapter<DMA_TypeDef> {
public:
	struct Peripheral {
		static inline constexpr DMAPeripheralOption MemoryToMemory          { LL_DMAMUX_REQ_MEM2MEM };
		static inline constexpr DMAPeripheralOption Generator0              { LL_DMAMUX_REQ_GENERATOR0 };
		static inline constexpr DMAPeripheralOption Generator1              { LL_DMAMUX_REQ_GENERATOR1 };
		static inline constexpr DMAPeripheralOption Generator2              { LL_DMAMUX_REQ_GENERATOR2 };
		static inline constexpr DMAPeripheralOption Generator3              { LL_DMAMUX_REQ_GENERATOR3 };
		static inline constexpr DMAPeripheralOption Adc1                    { LL_DMAMUX_REQ_ADC1 };
#if defined(AES)
		static inline constexpr DMAPeripheralOption AesIn                   { LL_DMAMUX_REQ_AES_IN };
		static inline constexpr DMAPeripheralOption AesOut                  { LL_DMAMUX_REQ_AES_OUT };
#endif
		static inline constexpr DMAPeripheralOption Dac1Channel1            { LL_DMAMUX_REQ_DAC1_CH1 };
		static inline constexpr DMAPeripheralOption Dac1Channel2            { LL_DMAMUX_REQ_DAC1_CH2 };
		static inline constexpr DMAPeripheralOption I2c1Rx                  { LL_DMAMUX_REQ_I2C1_RX };
		static inline constexpr DMAPeripheralOption I2c1Tx                  { LL_DMAMUX_REQ_I2C1_TX };
		static inline constexpr DMAPeripheralOption I2c2Rx                  { LL_DMAMUX_REQ_I2C2_RX };
		static inline constexpr DMAPeripheralOption I2c2Tx                  { LL_DMAMUX_REQ_I2C2_TX };
#if defined(LPUART1)
		static inline constexpr DMAPeripheralOption LpUart1Rx               { LL_DMAMUX_REQ_LPUART1_RX };
		static inline constexpr DMAPeripheralOption LpUart1Tx               { LL_DMAMUX_REQ_LPUART1_TX };
#endif
		static inline constexpr DMAPeripheralOption Spi1Rx                  { LL_DMAMUX_REQ_SPI1_RX };
		static inline constexpr DMAPeripheralOption Spi1Tx                  { LL_DMAMUX_REQ_SPI1_TX };
		static inline constexpr DMAPeripheralOption Spi2Rx                  { LL_DMAMUX_REQ_SPI2_RX };
		static inline constexpr DMAPeripheralOption Spi2Tx                  { LL_DMAMUX_REQ_SPI2_TX };
		static inline constexpr DMAPeripheralOption Timer1Channel1          { LL_DMAMUX_REQ_TIM1_CH1 };
		static inline constexpr DMAPeripheralOption Timer1Channel2          { LL_DMAMUX_REQ_TIM1_CH2 };
		static inline constexpr DMAPeripheralOption Timer1Channel3          { LL_DMAMUX_REQ_TIM1_CH3 };
		static inline constexpr DMAPeripheralOption Timer1Channel4          { LL_DMAMUX_REQ_TIM1_CH4 };
		static inline constexpr DMAPeripheralOption Timer1TriggerCommutation { LL_DMAMUX_REQ_TIM1_TRIG_COM };
		static inline constexpr DMAPeripheralOption Timer1Update            { LL_DMAMUX_REQ_TIM1_UP };
#if defined(TIM2)
		static inline constexpr DMAPeripheralOption Timer2Channel1          { LL_DMAMUX_REQ_TIM2_CH1 };
		static inline constexpr DMAPeripheralOption Timer2Channel2          { LL_DMAMUX_REQ_TIM2_CH2 };
		static inline constexpr DMAPeripheralOption Timer2Channel3          { LL_DMAMUX_REQ_TIM2_CH3 };
		static inline constexpr DMAPeripheralOption Timer2Channel4          { LL_DMAMUX_REQ_TIM2_CH4 };
		static inline constexpr DMAPeripheralOption Timer2Trigger           { LL_DMAMUX_REQ_TIM2_TRIG };
		static inline constexpr DMAPeripheralOption Timer2Update            { LL_DMAMUX_REQ_TIM2_UP };
#endif
		static inline constexpr DMAPeripheralOption Timer3Channel1          { LL_DMAMUX_REQ_TIM3_CH1 };
		static inline constexpr DMAPeripheralOption Timer3Channel2          { LL_DMAMUX_REQ_TIM3_CH2 };
		static inline constexpr DMAPeripheralOption Timer3Channel3          { LL_DMAMUX_REQ_TIM3_CH3 };
		static inline constexpr DMAPeripheralOption Timer3Channel4          { LL_DMAMUX_REQ_TIM3_CH4 };
		static inline constexpr DMAPeripheralOption Timer3Trigger           { LL_DMAMUX_REQ_TIM3_TRIG };
		static inline constexpr DMAPeripheralOption Timer3Update            { LL_DMAMUX_REQ_TIM3_UP };
#if defined(TIM6)
		static inline constexpr DMAPeripheralOption Timer6Update            { LL_DMAMUX_REQ_TIM6_UP };
#endif
#if defined(TIM7)
		static inline constexpr DMAPeripheralOption Timer7Update            { LL_DMAMUX_REQ_TIM7_UP };
#endif
#if defined(TIM15)
		static inline constexpr DMAPeripheralOption Timer15Channel1         { LL_DMAMUX_REQ_TIM15_CH1 };
		static inline constexpr DMAPeripheralOption Timer15Channel2         { LL_DMAMUX_REQ_TIM15_CH2 };
		static inline constexpr DMAPeripheralOption Timer15TriggerCommutation { LL_DMAMUX_REQ_TIM15_TRIG_COM };
		static inline constexpr DMAPeripheralOption Timer15Update           { LL_DMAMUX_REQ_TIM15_UP };
#endif
#if defined(TIM16)
		static inline constexpr DMAPeripheralOption Timer16Channel1         { LL_DMAMUX_REQ_TIM16_CH1 };
		static inline constexpr DMAPeripheralOption Timer16Commutation      { LL_DMAMUX_REQ_TIM16_COM };
		static inline constexpr DMAPeripheralOption Timer16Update           { LL_DMAMUX_REQ_TIM16_UP };
#endif
#if defined(TIM17)
		static inline constexpr DMAPeripheralOption Timer17Channel1         { LL_DMAMUX_REQ_TIM17_CH1 };
		static inline constexpr DMAPeripheralOption Timer17Commutation      { LL_DMAMUX_REQ_TIM17_COM };
		static inline constexpr DMAPeripheralOption Timer17Update           { LL_DMAMUX_REQ_TIM17_UP };
#endif
		static inline constexpr DMAPeripheralOption Usart1Rx                { LL_DMAMUX_REQ_USART1_RX };
		static inline constexpr DMAPeripheralOption Usart1Tx                { LL_DMAMUX_REQ_USART1_TX };
		static inline constexpr DMAPeripheralOption Usart2Rx                { LL_DMAMUX_REQ_USART2_RX };
		static inline constexpr DMAPeripheralOption Usart2Tx                { LL_DMAMUX_REQ_USART2_TX };
#if defined(USART3)
		static inline constexpr DMAPeripheralOption Usart3Rx                { LL_DMAMUX_REQ_USART3_RX };
		static inline constexpr DMAPeripheralOption Usart3Tx                { LL_DMAMUX_REQ_USART3_TX };
#endif
#if defined(USART4)
		static inline constexpr DMAPeripheralOption Usart4Rx                { LL_DMAMUX_REQ_USART4_RX };
		static inline constexpr DMAPeripheralOption Usart4Tx                { LL_DMAMUX_REQ_USART4_TX };
#endif
#if defined(UCPD1)
		static inline constexpr DMAPeripheralOption Ucpd1Rx                 { LL_DMAMUX_REQ_UCPD1_RX };
		static inline constexpr DMAPeripheralOption Ucpd1Tx                 { LL_DMAMUX_REQ_UCPD1_TX };
#endif
#if defined(UCPD2)
		static inline constexpr DMAPeripheralOption Ucpd2Rx                 { LL_DMAMUX_REQ_UCPD2_RX };
		static inline constexpr DMAPeripheralOption Ucpd2Tx                 { LL_DMAMUX_REQ_UCPD2_TX };
#endif
#if defined(I2C3)
		static inline constexpr DMAPeripheralOption I2c3Rx                  { LL_DMAMUX_REQ_I2C3_RX };
		static inline constexpr DMAPeripheralOption I2c3Tx                  { LL_DMAMUX_REQ_I2C3_TX };
#endif
#if defined(LPUART2)
		static inline constexpr DMAPeripheralOption LpUart2Rx               { LL_DMAMUX_REQ_LPUART2_RX };
		static inline constexpr DMAPeripheralOption LpUart2Tx               { LL_DMAMUX_REQ_LPUART2_TX };
#endif
#if defined(SPI3)
		static inline constexpr DMAPeripheralOption Spi3Rx                  { LL_DMAMUX_REQ_SPI3_RX };
		static inline constexpr DMAPeripheralOption Spi3Tx                  { LL_DMAMUX_REQ_SPI3_TX };
#endif
#if defined(TIM4)
		static inline constexpr DMAPeripheralOption Timer4Channel1          { LL_DMAMUX_REQ_TIM4_CH1 };
		static inline constexpr DMAPeripheralOption Timer4Channel2          { LL_DMAMUX_REQ_TIM4_CH2 };
		static inline constexpr DMAPeripheralOption Timer4Channel3          { LL_DMAMUX_REQ_TIM4_CH3 };
		static inline constexpr DMAPeripheralOption Timer4Channel4          { LL_DMAMUX_REQ_TIM4_CH4 };
		static inline constexpr DMAPeripheralOption Timer4Trigger           { LL_DMAMUX_REQ_TIM4_TRIG };
		static inline constexpr DMAPeripheralOption Timer4Update            { LL_DMAMUX_REQ_TIM4_UP };
#endif
#if defined(USART5)
		static inline constexpr DMAPeripheralOption Usart5Rx                { LL_DMAMUX_REQ_USART5_RX };
		static inline constexpr DMAPeripheralOption Usart5Tx                { LL_DMAMUX_REQ_USART5_TX };
#endif
#if defined(USART6)
		static inline constexpr DMAPeripheralOption Usart6Rx                { LL_DMAMUX_REQ_USART6_RX };
		static inline constexpr DMAPeripheralOption Usart6Tx                { LL_DMAMUX_REQ_USART6_TX };
#endif
	};



protected:
	volatile bool running;
	volatile bool transferComplete;
	volatile bool transferError;



private:
	static inline constexpr uint32 channelCount { 7 };



public:
	DMAAdapterG0()
		: DMAAdapter(), running(false), transferComplete(false), transferError(false) { }

	DMAAdapterG0(DMA_TypeDef *dma, uint32 channel)
		: DMAAdapter(dma, channel), running(false), transferComplete(false), transferError(false) { }



	// Applies the stored parameters and calls the hooks. A start applies them without the hooks, see StartTransfer().
	virtual ResultStatus Initialization() override {
		ResultStatus status = CheckConfiguration();
		if (status != ResultStatus::ok) {
			return status;
		}

		status = BeforeInitialization();
		if (status != ResultStatus::ok) {
			return status;
		}

		WriteChannelConfiguration();
		return AfterInitialization();
	}




	virtual ResultStatus StartTransfer(const uint8* from, uint8* to, uint32 size) override {
		if (!IsChannelValid()) {
			return ResultStatus::invalidParameter;
		}

		// The counter is defined only for equal widths, see CheckWidths().
		ResultStatus status = CheckWidths();
		if (status != ResultStatus::ok) {
			return status;
		}

		uint32 count = 0;
		status = ItemCount(size, ItemBytes(), 0xFFFFu, count);
		if (status != ResultStatus::ok) {
			return status;
		}

		// The channel must be idle: a run in progress is not aborted, so the start returns busy instead.
		status = CheckConfiguration();
		if (status != ResultStatus::ok) {
			return status;
		}

		// Applies the stored parameters without the hooks, so a start does not run the user's init callbacks.
		WriteChannelConfiguration();

		// Flags left by an earlier run would fire as soon as the channel is enabled.
		ClearAllFlags();

		if (parameters.direction == Direction::MemoryToPeripheral) {
			LL_DMA_SetMemoryAddress(dmaHandle, LLChannel(), (uint32)from);
			LL_DMA_SetPeriphAddress(dmaHandle, LLChannel(), (uint32)to);
		} else {
			LL_DMA_SetPeriphAddress(dmaHandle, LLChannel(), (uint32)from);
			LL_DMA_SetMemoryAddress(dmaHandle, LLChannel(), (uint32)to);
		}

		LL_DMA_SetDataLength(dmaHandle, LLChannel(), count);

		if (parameters.enableTransferCompleteIT) {
			LL_DMA_EnableIT_TC(dmaHandle, LLChannel());
		} else {
			LL_DMA_DisableIT_TC(dmaHandle, LLChannel());
		}

		if (onHalfTransfer) {
			LL_DMA_EnableIT_HT(dmaHandle, LLChannel());
		} else {
			LL_DMA_DisableIT_HT(dmaHandle, LLChannel());
		}

		LL_DMA_EnableIT_TE(dmaHandle, LLChannel());

		transferComplete = false;
		transferError = false;
		lastTransferSize = count;
		LatchRunMode();

		running = true;
		LL_DMA_EnableChannel(dmaHandle, LLChannel());
		return ResultStatus::ok;
	}




	// Stops the channel and its interrupts. The callbacks of a run stopped this way are not called.
	virtual ResultStatus Stop() override {
		if (!IsChannelValid()) {
			return ResultStatus::invalidParameter;
		}

		running = false;
		LL_DMA_DisableChannel(dmaHandle, LLChannel());
		LL_DMA_DisableIT_TC(dmaHandle, LLChannel());
		LL_DMA_DisableIT_HT(dmaHandle, LLChannel());
		LL_DMA_DisableIT_TE(dmaHandle, LLChannel());
		return ResultStatus::ok;
	}




	// The completion and the error are latched by the interrupt, so the result stays right after the flags are cleared.
	virtual ResultStatus GetStatus() override {
		if (!IsChannelValid()) {
			return ResultStatus::invalidParameter;
		}

		return LatchedStatus(
			transferError || IsActiveFlag_TE(),
			transferComplete || IsActiveFlag_TC(),
			IsRunCircular(),
			IsChannelEnabled()
		);
	}




	virtual inline void IrqHandler() override {
		if (!IsChannelValid()) {
			return;
		}

		// A stopped channel can still raise its flags while it drains. Clear them and report nothing.
		if (!running) {
			ClearAllFlags();
			return;
		}

		// DMA transfer error. The channel stops in hardware, and it is disabled here as well.
		if (IsActiveFlag_TE() && LL_DMA_IsEnabledIT_TE(dmaHandle, LLChannel())) {
			LL_DMA_DisableChannel(dmaHandle, LLChannel());
			LL_DMA_DisableIT_TE(dmaHandle, LLChannel());
			LL_DMA_DisableIT_TC(dmaHandle, LLChannel());
			LL_DMA_DisableIT_HT(dmaHandle, LLChannel());

			ClearAllFlags();
			running = false;
			transferError = true;
			CallError();
			return;
		}

		// DMA half transfer. Normal mode raises it once, circular mode for every half.
		if (IsActiveFlag_HT() && LL_DMA_IsEnabledIT_HT(dmaHandle, LLChannel())) {
			if (!IsRunCircular()) {
				LL_DMA_DisableIT_HT(dmaHandle, LLChannel());
			}

			ClearFlag_HT();
			CallHalfTransfer();
		}


		// DMA transfer complete. In normal mode the channel has stopped, so the run ends here.
		if (IsActiveFlag_TC() && LL_DMA_IsEnabledIT_TC(dmaHandle, LLChannel())) {
			if (!IsRunCircular()) {
				running = false;
				LL_DMA_DisableIT_TC(dmaHandle, LLChannel());
				LL_DMA_DisableIT_HT(dmaHandle, LLChannel());
				LL_DMA_DisableIT_TE(dmaHandle, LLChannel());
			}

			ClearFlag_TC();
			transferComplete = true;
			CallTransferComplete();
		}
	}


protected:
	bool IsChannelValid() const {
		return dmaHandle == DMA1 && dmaChannel >= 1 && dmaChannel <= channelCount;
	}


	// The checks of a configuration. The channel is busy while it runs: CCR is write-protected, and the stored parameters are
	// written by the next call that finds the channel idle.
	ResultStatus CheckConfiguration() const {
		if (!IsChannelValid()) {
			return ResultStatus::invalidParameter;
		}

		if (parameters.priority > 3) {
			return ResultStatus::invalidParameter;
		}

		if (IsChannelEnabled()) {
			return ResultStatus::busy;
		}

		return ResultStatus::ok;
	}


	// Writes the stored parameters to the channel. It does not call the hooks.
	void WriteChannelConfiguration() {
		LL_DMA_SetPeriphRequest(dmaHandle, LLChannel(), parameters.peripheral.Get());
		LL_DMA_SetDataTransferDirection(dmaHandle, LLChannel(), CastDirection());
		LL_DMA_SetChannelPriorityLevel(dmaHandle, LLChannel(), CastPriority());
		LL_DMA_SetMode(dmaHandle, LLChannel(), parameters.circularMode ? LL_DMA_MODE_CIRCULAR : LL_DMA_MODE_NORMAL);
		LL_DMA_SetPeriphIncMode(dmaHandle, LLChannel(), CastPeriphIncMode());
		LL_DMA_SetMemoryIncMode(dmaHandle, LLChannel(), CastMemoryIncMode());
		LL_DMA_SetPeriphSize(dmaHandle, LLChannel(), CastPeriphDataWidth());
		LL_DMA_SetMemorySize(dmaHandle, LLChannel(), CastMemoryDataWidth());
	}


	bool IsChannelEnabled() const {
		return LL_DMA_IsEnabledChannel(dmaHandle, LLChannel());
	}


	void ClearAllFlags() {
		ClearFlag_TC();
		ClearFlag_HT();
		ClearFlag_TE();
	}


	bool IsActiveFlag_HT() {
		switch (dmaChannel) {
			case 1: return LL_DMA_IsActiveFlag_HT1(dmaHandle);
			case 2: return LL_DMA_IsActiveFlag_HT2(dmaHandle);
			case 3: return LL_DMA_IsActiveFlag_HT3(dmaHandle);
			case 4: return LL_DMA_IsActiveFlag_HT4(dmaHandle);
			case 5: return LL_DMA_IsActiveFlag_HT5(dmaHandle);
			case 6: return LL_DMA_IsActiveFlag_HT6(dmaHandle);
			case 7: return LL_DMA_IsActiveFlag_HT7(dmaHandle);
			default: SystemAbort();
		}
		return false;
	}


	bool IsActiveFlag_TC() {
		switch (dmaChannel) {
			case 1: return LL_DMA_IsActiveFlag_TC1(dmaHandle);
			case 2: return LL_DMA_IsActiveFlag_TC2(dmaHandle);
			case 3: return LL_DMA_IsActiveFlag_TC3(dmaHandle);
			case 4: return LL_DMA_IsActiveFlag_TC4(dmaHandle);
			case 5: return LL_DMA_IsActiveFlag_TC5(dmaHandle);
			case 6: return LL_DMA_IsActiveFlag_TC6(dmaHandle);
			case 7: return LL_DMA_IsActiveFlag_TC7(dmaHandle);
			default: SystemAbort();
		}
		return false;
	}


	bool IsActiveFlag_TE() {
		switch (dmaChannel) {
			case 1: return LL_DMA_IsActiveFlag_TE1(dmaHandle);
			case 2: return LL_DMA_IsActiveFlag_TE2(dmaHandle);
			case 3: return LL_DMA_IsActiveFlag_TE3(dmaHandle);
			case 4: return LL_DMA_IsActiveFlag_TE4(dmaHandle);
			case 5: return LL_DMA_IsActiveFlag_TE5(dmaHandle);
			case 6: return LL_DMA_IsActiveFlag_TE6(dmaHandle);
			case 7: return LL_DMA_IsActiveFlag_TE7(dmaHandle);
			default: SystemAbort();
		}
		return false;
	}


	void ClearFlag_HT() {
		switch (dmaChannel) {
			case 1: LL_DMA_ClearFlag_HT1(dmaHandle); break;
			case 2: LL_DMA_ClearFlag_HT2(dmaHandle); break;
			case 3: LL_DMA_ClearFlag_HT3(dmaHandle); break;
			case 4: LL_DMA_ClearFlag_HT4(dmaHandle); break;
			case 5: LL_DMA_ClearFlag_HT5(dmaHandle); break;
			case 6: LL_DMA_ClearFlag_HT6(dmaHandle); break;
			case 7: LL_DMA_ClearFlag_HT7(dmaHandle); break;
			default: SystemAbort();
		}
	}


	void ClearFlag_TC() {
		switch (dmaChannel) {
			case 1: LL_DMA_ClearFlag_TC1(dmaHandle); break;
			case 2: LL_DMA_ClearFlag_TC2(dmaHandle); break;
			case 3: LL_DMA_ClearFlag_TC3(dmaHandle); break;
			case 4: LL_DMA_ClearFlag_TC4(dmaHandle); break;
			case 5: LL_DMA_ClearFlag_TC5(dmaHandle); break;
			case 6: LL_DMA_ClearFlag_TC6(dmaHandle); break;
			case 7: LL_DMA_ClearFlag_TC7(dmaHandle); break;
			default: SystemAbort();
		}
	}


	void ClearFlag_TE() {
		switch (dmaChannel) {
			case 1: LL_DMA_ClearFlag_TE1(dmaHandle); break;
			case 2: LL_DMA_ClearFlag_TE2(dmaHandle); break;
			case 3: LL_DMA_ClearFlag_TE3(dmaHandle); break;
			case 4: LL_DMA_ClearFlag_TE4(dmaHandle); break;
			case 5: LL_DMA_ClearFlag_TE5(dmaHandle); break;
			case 6: LL_DMA_ClearFlag_TE6(dmaHandle); break;
			case 7: LL_DMA_ClearFlag_TE7(dmaHandle); break;
			default: SystemAbort();
		}
	}


private:
	// LL API uses 0-based channel index (LL_DMA_CHANNEL_1 = 0),
	// but adapter stores 1-based channel number for flag switch-cases
	constexpr uint32 LLChannel() const { return dmaChannel - 1; }


	constexpr uint32 CastDirection() const {
		switch (parameters.direction) {
			case Direction::MemoryToPeripheral: return LL_DMA_DIRECTION_MEMORY_TO_PERIPH;
			case Direction::PeripheralToMemory: return LL_DMA_DIRECTION_PERIPH_TO_MEMORY;
			case Direction::MemoryToMemory:     return LL_DMA_DIRECTION_MEMORY_TO_MEMORY;
		}
		SystemAbort();
		return 0;
	}


	constexpr uint32 CastPriority() const {
		switch (parameters.priority) {
			case 0: return LL_DMA_PRIORITY_LOW;
			case 1: return LL_DMA_PRIORITY_MEDIUM;
			case 2: return LL_DMA_PRIORITY_HIGH;
			case 3: return LL_DMA_PRIORITY_VERYHIGH;
		}
		return LL_DMA_PRIORITY_LOW;
	}


	constexpr uint32 CastPeriphDataWidth() const {
		switch (GetPeriphDataWidth()) {
			case DataWidth::B8:  return LL_DMA_PDATAALIGN_BYTE;
			case DataWidth::B16: return LL_DMA_PDATAALIGN_HALFWORD;
			case DataWidth::B32: return LL_DMA_PDATAALIGN_WORD;
		}
		SystemAbort();
		return 0;
	}


	constexpr uint32 CastMemoryDataWidth() const {
		switch (parameters.dataWidth) {
			case DataWidth::B8:  return LL_DMA_MDATAALIGN_BYTE;
			case DataWidth::B16: return LL_DMA_MDATAALIGN_HALFWORD;
			case DataWidth::B32: return LL_DMA_MDATAALIGN_WORD;
		}
		SystemAbort();
		return 0;
	}


	// The transfer counter counts items of the width both sides share, which CheckWidths() has matched.
	constexpr uint32 ItemBytes() const {
		return static_cast<uint32>(parameters.dataWidth) / 8;
	}


	constexpr uint32 CastPeriphIncMode() const {
		switch (parameters.addressMode) {
			case AddressMode::FixedToIncrementing:       	return LL_DMA_PERIPH_NOINCREMENT;
			case AddressMode::IncrementingToFixed:        	return LL_DMA_PERIPH_NOINCREMENT;
			case AddressMode::IncrementingToIncrementing: 	return LL_DMA_PERIPH_INCREMENT;
		}
		SystemAbort();
		return 0;
	}


	constexpr uint32 CastMemoryIncMode() const {
		switch (parameters.addressMode) {
			case AddressMode::FixedToIncrementing:       	return LL_DMA_MEMORY_INCREMENT;
			case AddressMode::IncrementingToFixed:        	return LL_DMA_MEMORY_INCREMENT;
			case AddressMode::IncrementingToIncrementing: 	return LL_DMA_MEMORY_INCREMENT;
		}
		SystemAbort();
		return 0;
	}
};
