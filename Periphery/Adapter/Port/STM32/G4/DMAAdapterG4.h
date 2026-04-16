#pragma once
#include <Adapter/DMAAdapter.h>

using ADMA = class DMAAdapterG4;


class DMAAdapterG4: public DMAAdapter<DMA_TypeDef> {
public:
	struct Peripheral {
		static inline constexpr DMAPeripheralOption MemoryToMemory          { LL_DMAMUX_REQ_MEM2MEM };
		static inline constexpr DMAPeripheralOption Generator0              { LL_DMAMUX_REQ_GENERATOR0 };
		static inline constexpr DMAPeripheralOption Generator1              { LL_DMAMUX_REQ_GENERATOR1 };
		static inline constexpr DMAPeripheralOption Generator2              { LL_DMAMUX_REQ_GENERATOR2 };
		static inline constexpr DMAPeripheralOption Generator3              { LL_DMAMUX_REQ_GENERATOR3 };
		static inline constexpr DMAPeripheralOption Adc1                    { LL_DMAMUX_REQ_ADC1 };
		static inline constexpr DMAPeripheralOption Dac1Channel1            { LL_DMAMUX_REQ_DAC1_CH1 };
		static inline constexpr DMAPeripheralOption Dac1Channel2            { LL_DMAMUX_REQ_DAC1_CH2 };
		static inline constexpr DMAPeripheralOption Timer6Update            { LL_DMAMUX_REQ_TIM6_UP };
		static inline constexpr DMAPeripheralOption Timer7Update            { LL_DMAMUX_REQ_TIM7_UP };
		static inline constexpr DMAPeripheralOption Spi1Rx                  { LL_DMAMUX_REQ_SPI1_RX };
		static inline constexpr DMAPeripheralOption Spi1Tx                  { LL_DMAMUX_REQ_SPI1_TX };
		static inline constexpr DMAPeripheralOption Spi2Rx                  { LL_DMAMUX_REQ_SPI2_RX };
		static inline constexpr DMAPeripheralOption Spi2Tx                  { LL_DMAMUX_REQ_SPI2_TX };
		static inline constexpr DMAPeripheralOption Spi3Rx                  { LL_DMAMUX_REQ_SPI3_RX };
		static inline constexpr DMAPeripheralOption Spi3Tx                  { LL_DMAMUX_REQ_SPI3_TX };
		static inline constexpr DMAPeripheralOption I2c1Rx                  { LL_DMAMUX_REQ_I2C1_RX };
		static inline constexpr DMAPeripheralOption I2c1Tx                  { LL_DMAMUX_REQ_I2C1_TX };
		static inline constexpr DMAPeripheralOption I2c2Rx                  { LL_DMAMUX_REQ_I2C2_RX };
		static inline constexpr DMAPeripheralOption I2c2Tx                  { LL_DMAMUX_REQ_I2C2_TX };
		static inline constexpr DMAPeripheralOption I2c3Rx                  { LL_DMAMUX_REQ_I2C3_RX };
		static inline constexpr DMAPeripheralOption I2c3Tx                  { LL_DMAMUX_REQ_I2C3_TX };
#if defined(I2C4)
		static inline constexpr DMAPeripheralOption I2c4Rx                  { LL_DMAMUX_REQ_I2C4_RX };
		static inline constexpr DMAPeripheralOption I2c4Tx                  { LL_DMAMUX_REQ_I2C4_TX };
#endif
		static inline constexpr DMAPeripheralOption Usart1Rx                { LL_DMAMUX_REQ_USART1_RX };
		static inline constexpr DMAPeripheralOption Usart1Tx                { LL_DMAMUX_REQ_USART1_TX };
		static inline constexpr DMAPeripheralOption Usart2Rx                { LL_DMAMUX_REQ_USART2_RX };
		static inline constexpr DMAPeripheralOption Usart2Tx                { LL_DMAMUX_REQ_USART2_TX };
		static inline constexpr DMAPeripheralOption Usart3Rx                { LL_DMAMUX_REQ_USART3_RX };
		static inline constexpr DMAPeripheralOption Usart3Tx                { LL_DMAMUX_REQ_USART3_TX };
#if defined(UART4)
		static inline constexpr DMAPeripheralOption Uart4Rx                 { LL_DMAMUX_REQ_UART4_RX };
		static inline constexpr DMAPeripheralOption Uart4Tx                 { LL_DMAMUX_REQ_UART4_TX };
#endif
#if defined(UART5)
		static inline constexpr DMAPeripheralOption Uart5Rx                 { LL_DMAMUX_REQ_UART5_RX };
		static inline constexpr DMAPeripheralOption Uart5Tx                 { LL_DMAMUX_REQ_UART5_TX };
#endif
		static inline constexpr DMAPeripheralOption LpUart1Rx               { LL_DMAMUX_REQ_LPUART1_RX };
		static inline constexpr DMAPeripheralOption LpUart1Tx               { LL_DMAMUX_REQ_LPUART1_TX };
		static inline constexpr DMAPeripheralOption Adc2                    { LL_DMAMUX_REQ_ADC2 };
#if defined(ADC3)
		static inline constexpr DMAPeripheralOption Adc3                    { LL_DMAMUX_REQ_ADC3 };
#endif
#if defined(ADC4)
		static inline constexpr DMAPeripheralOption Adc4                    { LL_DMAMUX_REQ_ADC4 };
#endif
#if defined(ADC5)
		static inline constexpr DMAPeripheralOption Adc5                    { LL_DMAMUX_REQ_ADC5 };
#endif
#if defined(QUADSPI)
		static inline constexpr DMAPeripheralOption Qspi                    { LL_DMAMUX_REQ_QSPI };
#endif
#if defined(DAC2)
		static inline constexpr DMAPeripheralOption Dac2Channel1            { LL_DMAMUX_REQ_DAC2_CH1 };
#endif
		static inline constexpr DMAPeripheralOption Timer1Channel1          { LL_DMAMUX_REQ_TIM1_CH1 };
		static inline constexpr DMAPeripheralOption Timer1Channel2          { LL_DMAMUX_REQ_TIM1_CH2 };
		static inline constexpr DMAPeripheralOption Timer1Channel3          { LL_DMAMUX_REQ_TIM1_CH3 };
		static inline constexpr DMAPeripheralOption Timer1Channel4          { LL_DMAMUX_REQ_TIM1_CH4 };
		static inline constexpr DMAPeripheralOption Timer1Update            { LL_DMAMUX_REQ_TIM1_UP };
		static inline constexpr DMAPeripheralOption Timer1Trigger           { LL_DMAMUX_REQ_TIM1_TRIG };
		static inline constexpr DMAPeripheralOption Timer1Commutation       { LL_DMAMUX_REQ_TIM1_COM };
		static inline constexpr DMAPeripheralOption Timer8Channel1          { LL_DMAMUX_REQ_TIM8_CH1 };
		static inline constexpr DMAPeripheralOption Timer8Channel2          { LL_DMAMUX_REQ_TIM8_CH2 };
		static inline constexpr DMAPeripheralOption Timer8Channel3          { LL_DMAMUX_REQ_TIM8_CH3 };
		static inline constexpr DMAPeripheralOption Timer8Channel4          { LL_DMAMUX_REQ_TIM8_CH4 };
		static inline constexpr DMAPeripheralOption Timer8Update            { LL_DMAMUX_REQ_TIM8_UP };
		static inline constexpr DMAPeripheralOption Timer8Trigger           { LL_DMAMUX_REQ_TIM8_TRIG };
		static inline constexpr DMAPeripheralOption Timer8Commutation       { LL_DMAMUX_REQ_TIM8_COM };
		static inline constexpr DMAPeripheralOption Timer2Channel1          { LL_DMAMUX_REQ_TIM2_CH1 };
		static inline constexpr DMAPeripheralOption Timer2Channel2          { LL_DMAMUX_REQ_TIM2_CH2 };
		static inline constexpr DMAPeripheralOption Timer2Channel3          { LL_DMAMUX_REQ_TIM2_CH3 };
		static inline constexpr DMAPeripheralOption Timer2Channel4          { LL_DMAMUX_REQ_TIM2_CH4 };
		static inline constexpr DMAPeripheralOption Timer2Update            { LL_DMAMUX_REQ_TIM2_UP };
		static inline constexpr DMAPeripheralOption Timer3Channel1          { LL_DMAMUX_REQ_TIM3_CH1 };
		static inline constexpr DMAPeripheralOption Timer3Channel2          { LL_DMAMUX_REQ_TIM3_CH2 };
		static inline constexpr DMAPeripheralOption Timer3Channel3          { LL_DMAMUX_REQ_TIM3_CH3 };
		static inline constexpr DMAPeripheralOption Timer3Channel4          { LL_DMAMUX_REQ_TIM3_CH4 };
		static inline constexpr DMAPeripheralOption Timer3Update            { LL_DMAMUX_REQ_TIM3_UP };
		static inline constexpr DMAPeripheralOption Timer3Trigger           { LL_DMAMUX_REQ_TIM3_TRIG };
		static inline constexpr DMAPeripheralOption Timer4Channel1          { LL_DMAMUX_REQ_TIM4_CH1 };
		static inline constexpr DMAPeripheralOption Timer4Channel2          { LL_DMAMUX_REQ_TIM4_CH2 };
		static inline constexpr DMAPeripheralOption Timer4Channel3          { LL_DMAMUX_REQ_TIM4_CH3 };
		static inline constexpr DMAPeripheralOption Timer4Channel4          { LL_DMAMUX_REQ_TIM4_CH4 };
		static inline constexpr DMAPeripheralOption Timer4Update            { LL_DMAMUX_REQ_TIM4_UP };
#if defined(TIM5)
		static inline constexpr DMAPeripheralOption Timer5Channel1          { LL_DMAMUX_REQ_TIM5_CH1 };
		static inline constexpr DMAPeripheralOption Timer5Channel2          { LL_DMAMUX_REQ_TIM5_CH2 };
		static inline constexpr DMAPeripheralOption Timer5Channel3          { LL_DMAMUX_REQ_TIM5_CH3 };
		static inline constexpr DMAPeripheralOption Timer5Channel4          { LL_DMAMUX_REQ_TIM5_CH4 };
		static inline constexpr DMAPeripheralOption Timer5Update            { LL_DMAMUX_REQ_TIM5_UP };
		static inline constexpr DMAPeripheralOption Timer5Trigger           { LL_DMAMUX_REQ_TIM5_TRIG };
#endif
		static inline constexpr DMAPeripheralOption Timer15Channel1         { LL_DMAMUX_REQ_TIM15_CH1 };
		static inline constexpr DMAPeripheralOption Timer15Update           { LL_DMAMUX_REQ_TIM15_UP };
		static inline constexpr DMAPeripheralOption Timer15Trigger          { LL_DMAMUX_REQ_TIM15_TRIG };
		static inline constexpr DMAPeripheralOption Timer15Commutation      { LL_DMAMUX_REQ_TIM15_COM };
		static inline constexpr DMAPeripheralOption Timer16Channel1         { LL_DMAMUX_REQ_TIM16_CH1 };
		static inline constexpr DMAPeripheralOption Timer16Update           { LL_DMAMUX_REQ_TIM16_UP };
		static inline constexpr DMAPeripheralOption Timer17Channel1         { LL_DMAMUX_REQ_TIM17_CH1 };
		static inline constexpr DMAPeripheralOption Timer17Update           { LL_DMAMUX_REQ_TIM17_UP };
#if defined(TIM20)
		static inline constexpr DMAPeripheralOption Timer20Channel1         { LL_DMAMUX_REQ_TIM20_CH1 };
		static inline constexpr DMAPeripheralOption Timer20Channel2         { LL_DMAMUX_REQ_TIM20_CH2 };
		static inline constexpr DMAPeripheralOption Timer20Channel3         { LL_DMAMUX_REQ_TIM20_CH3 };
		static inline constexpr DMAPeripheralOption Timer20Channel4         { LL_DMAMUX_REQ_TIM20_CH4 };
		static inline constexpr DMAPeripheralOption Timer20Update           { LL_DMAMUX_REQ_TIM20_UP };
		static inline constexpr DMAPeripheralOption Timer20Trigger          { LL_DMAMUX_REQ_TIM20_TRIG };
		static inline constexpr DMAPeripheralOption Timer20Commutation      { LL_DMAMUX_REQ_TIM20_COM };
#endif
#if defined(AES)
		static inline constexpr DMAPeripheralOption AesIn                   { LL_DMAMUX_REQ_AES_IN };
		static inline constexpr DMAPeripheralOption AesOut                  { LL_DMAMUX_REQ_AES_OUT };
#endif
#if defined(DAC3)
		static inline constexpr DMAPeripheralOption Dac3Channel1            { LL_DMAMUX_REQ_DAC3_CH1 };
		static inline constexpr DMAPeripheralOption Dac3Channel2            { LL_DMAMUX_REQ_DAC3_CH2 };
#endif
#if defined(DAC4)
		static inline constexpr DMAPeripheralOption Dac4Channel1            { LL_DMAMUX_REQ_DAC4_CH1 };
		static inline constexpr DMAPeripheralOption Dac4Channel2            { LL_DMAMUX_REQ_DAC4_CH2 };
#endif
#if defined(SPI4)
		static inline constexpr DMAPeripheralOption Spi4Rx                  { LL_DMAMUX_REQ_SPI4_RX };
		static inline constexpr DMAPeripheralOption Spi4Tx                  { LL_DMAMUX_REQ_SPI4_TX };
#endif
#if defined(SAI1)
		static inline constexpr DMAPeripheralOption Sai1A                   { LL_DMAMUX_REQ_SAI1_A };
		static inline constexpr DMAPeripheralOption Sai1B                   { LL_DMAMUX_REQ_SAI1_B };
#endif
#if defined(FMAC)
		static inline constexpr DMAPeripheralOption FmacRead                { LL_DMAMUX_REQ_FMAC_READ };
		static inline constexpr DMAPeripheralOption FmacWrite               { LL_DMAMUX_REQ_FMAC_WRITE };
#endif
#if defined(CORDIC)
		static inline constexpr DMAPeripheralOption CordicRead              { LL_DMAMUX_REQ_CORDIC_READ };
		static inline constexpr DMAPeripheralOption CordicWrite             { LL_DMAMUX_REQ_CORDIC_WRITE };
#endif
#if defined(UCPD1)
		static inline constexpr DMAPeripheralOption Ucpd1Rx                 { LL_DMAMUX_REQ_UCPD1_RX };
		static inline constexpr DMAPeripheralOption Ucpd1Tx                 { LL_DMAMUX_REQ_UCPD1_TX };
#endif
#if defined(HRTIM1)
		static inline constexpr DMAPeripheralOption Hrtim1M                 { LL_DMAMUX_REQ_HRTIM1_M };
		static inline constexpr DMAPeripheralOption Hrtim1A                 { LL_DMAMUX_REQ_HRTIM1_A };
		static inline constexpr DMAPeripheralOption Hrtim1B                 { LL_DMAMUX_REQ_HRTIM1_B };
		static inline constexpr DMAPeripheralOption Hrtim1C                 { LL_DMAMUX_REQ_HRTIM1_C };
		static inline constexpr DMAPeripheralOption Hrtim1D                 { LL_DMAMUX_REQ_HRTIM1_D };
		static inline constexpr DMAPeripheralOption Hrtim1E                 { LL_DMAMUX_REQ_HRTIM1_E };
		static inline constexpr DMAPeripheralOption Hrtim1F                 { LL_DMAMUX_REQ_HRTIM1_F };
#endif
	};



public:
	DMAAdapterG4() { }
	DMAAdapterG4(DMA_TypeDef *dma, uint32 channel): DMAAdapter(dma, channel) { }



	virtual ResultStatus Initialization() override {
		auto status = BeforeInitialization();
		if (status != ResultStatus::ok) {
			return status;
		}

		LL_DMA_SetPeriphRequest(dmaHandle, LLChannel(), parameters.peripheral.Get());
		LL_DMA_SetDataTransferDirection(dmaHandle, LLChannel(), CastDirection());
		LL_DMA_SetChannelPriorityLevel(dmaHandle, LLChannel(), CastPriority());
		LL_DMA_SetMode(dmaHandle, LLChannel(), parameters.circularMode ? LL_DMA_MODE_CIRCULAR : LL_DMA_MODE_NORMAL);
		LL_DMA_SetPeriphIncMode(dmaHandle, LLChannel(), CastPeriphIncMode());
		LL_DMA_SetMemoryIncMode(dmaHandle, LLChannel(), CastMemoryIncMode());
		LL_DMA_SetPeriphSize(dmaHandle, LLChannel(), CastPeriphDataWidth());
		LL_DMA_SetMemorySize(dmaHandle, LLChannel(), CastMemoryDataWidth());

		return AfterInitialization();
	}




	virtual ResultStatus StartTransfer(const uint8* from, uint8* to, uint32 size) override {
		uint32 count = size / DataWidthBytes();

		LL_DMA_DisableChannel(dmaHandle, LLChannel());

		ClearFlag_TC();
		ClearFlag_HT();
		ClearFlag_TE();

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
		}
		LL_DMA_EnableIT_TE(dmaHandle, LLChannel());

		LL_DMA_EnableChannel(dmaHandle, LLChannel());

		lastTransferSize = count;
		return ResultStatus::ok;
	}




	virtual ResultStatus Stop() override {
		LL_DMA_DisableChannel(dmaHandle, LLChannel());
		LL_DMA_DisableIT_TC(dmaHandle, LLChannel());
		LL_DMA_DisableIT_HT(dmaHandle, LLChannel());
		LL_DMA_DisableIT_TE(dmaHandle, LLChannel());
		return ResultStatus::ok;
	}




	virtual ResultStatus GetStatus() override {
		if (IsActiveFlag_TE()) {
			return ResultStatus::error;
		}
		if (IsActiveFlag_TC()) {
			return ResultStatus::ok;
		}
		return ResultStatus::busy;
	}




	virtual inline void IrqHandler() override {
		if (IsActiveFlag_HT() && LL_DMA_IsEnabledIT_HT(dmaHandle, LLChannel())) {
			if (LL_DMA_GetMode(dmaHandle, LLChannel()) == LL_DMA_MODE_NORMAL) {
				LL_DMA_DisableIT_HT(dmaHandle, LLChannel());
			}
			ClearFlag_HT();
			CallHalfTransfer();
		}

		if (IsActiveFlag_TC() && LL_DMA_IsEnabledIT_TC(dmaHandle, LLChannel())) {
			if (LL_DMA_GetMode(dmaHandle, LLChannel()) == LL_DMA_MODE_NORMAL) {
				LL_DMA_DisableIT_TC(dmaHandle, LLChannel());
				LL_DMA_DisableIT_HT(dmaHandle, LLChannel());
				LL_DMA_DisableIT_TE(dmaHandle, LLChannel());
			}
			ClearFlag_TC();
			CallTransferComplete();
		}

		if (IsActiveFlag_TE() && LL_DMA_IsEnabledIT_TE(dmaHandle, LLChannel())) {
			LL_DMA_DisableIT_TE(dmaHandle, LLChannel());
			LL_DMA_DisableIT_TC(dmaHandle, LLChannel());
			LL_DMA_DisableIT_HT(dmaHandle, LLChannel());
			ClearFlag_HT();
			ClearFlag_TC();
			ClearFlag_TE();
			CallError();
		}
	}


protected:
	bool IsActiveFlag_HT() {
		switch (dmaChannel) {
			case 1: return LL_DMA_IsActiveFlag_HT1(dmaHandle);
			case 2: return LL_DMA_IsActiveFlag_HT2(dmaHandle);
			case 3: return LL_DMA_IsActiveFlag_HT3(dmaHandle);
			case 4: return LL_DMA_IsActiveFlag_HT4(dmaHandle);
			case 5: return LL_DMA_IsActiveFlag_HT5(dmaHandle);
			case 6: return LL_DMA_IsActiveFlag_HT6(dmaHandle);
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
			default: SystemAbort();
		}
	}


private:
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
		DataWidth pw = (parameters.periphDataWidth != static_cast<DataWidth>(0))
			? parameters.periphDataWidth
			: parameters.dataWidth;
		switch (pw) {
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


	constexpr uint32 DataWidthBytes() const {
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
