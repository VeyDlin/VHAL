#pragma once
#include <Adapter/DACAdapter.h>
#include "DMAAdapterG4.h"


using ADAC = class DACAdapterG4;


class DACAdapterG4: public DACAdapter<DAC_TypeDef, ADMA> {
public:
	struct TriggerSource {
		static inline constexpr TriggerSourceOption Software        { LL_DAC_TRIG_SOFTWARE };
		static inline constexpr TriggerSourceOption Timer1Trigger   { LL_DAC_TRIG_EXT_TIM1_TRGO };
		static inline constexpr TriggerSourceOption Timer8Trigger   { LL_DAC_TRIG_EXT_TIM8_TRGO };
		static inline constexpr TriggerSourceOption Timer7Trigger   { LL_DAC_TRIG_EXT_TIM7_TRGO };
		static inline constexpr TriggerSourceOption Timer15Trigger  { LL_DAC_TRIG_EXT_TIM15_TRGO };
		static inline constexpr TriggerSourceOption Timer2Trigger   { LL_DAC_TRIG_EXT_TIM2_TRGO };
		static inline constexpr TriggerSourceOption Timer4Trigger   { LL_DAC_TRIG_EXT_TIM4_TRGO };
		static inline constexpr TriggerSourceOption Timer6Trigger   { LL_DAC_TRIG_EXT_TIM6_TRGO };
		static inline constexpr TriggerSourceOption Timer3Trigger   { LL_DAC_TRIG_EXT_TIM3_TRGO };
		static inline constexpr TriggerSourceOption InterruptLine9  { LL_DAC_TRIG_EXT_EXTI_LINE9 };
	};


public:
	DACAdapterG4() { }

	DACAdapterG4(DAC_TypeDef *dac, uint8 channel): DACAdapter(dac, channel) {

	}


	virtual inline void IrqHandler() override {
		DmaUnderrunInterrupt();
	}


	virtual ResultStatus Write(uint16 val) override {
		LL_DAC_ConvertData12RightAligned(dacHandle, CastChannel(), val);
		return ResultStatus::ok;
	}


	virtual ResultStatus Enable() override {
		if (LL_DAC_IsEnabled(dacHandle, CastChannel())) {
			return ResultStatus::ok;
		}

		LL_DAC_Enable(dacHandle, CastChannel());
		System::DelayUs(LL_DAC_DELAY_STARTUP_VOLTAGE_SETTLING_US);
		return ResultStatus::ok;
	}


	virtual ResultStatus Disable() override {
		if (LL_DAC_IsEnabled(dacHandle, CastChannel())) {
			LL_DAC_Disable(dacHandle, CastChannel());
		}
		return ResultStatus::ok;
	}


	virtual void EnableDMARequest() override {
		LL_DAC_EnableDMAReq(dacHandle, CastChannel());
	}


	virtual void DisableDMARequest() override {
		LL_DAC_DisableDMAReq(dacHandle, CastChannel());
	}


	virtual void EnableTrigger() override {
		LL_DAC_EnableTrigger(dacHandle, CastChannel());
	}


	virtual void DisableTrigger() override {
		LL_DAC_DisableTrigger(dacHandle, CastChannel());
	}


	// count is in samples of 16 bits. The DMA must move 16-bit items, so the count is not doubled.
	virtual ResultStatus WriteContinuous(uint16 *buffer, uint32 count) override {
		if (dma == nullptr) {
			return ResultStatus::notAvailable;
		}

		ResultStatus status = CheckDma();
		if (status != ResultStatus::ok) {
			return status;
		}

		status = dma->Start(buffer, (uint16*)GetDataRegisterAddress(), count);
		if (status != ResultStatus::ok) {
			return status;
		}
		ClearFlag_DmaUnderrun();
		EnableDMARequest();
		EnableTrigger();
		EnableIT_DmaUnderrun();
		return ResultStatus::ok;
	}


	virtual ResultStatus StopDMA() override {
		if (dma == nullptr) {
			return ResultStatus::notAvailable;
		}
		DisableIT_DmaUnderrun();
		DisableDMARequest();
		DisableTrigger();
		return dma->Stop();
	}


	virtual uint32 GetDataRegisterAddress() override {
		return LL_DAC_DMA_GetRegAddr(dacHandle, CastChannel(),
			LL_DAC_DMA_REG_DATA_12BITS_RIGHT_ALIGNED);
	}


protected:
	inline void DmaUnderrunInterrupt() {
		bool active, enabled;

		if (dacChannel == 1) {
			active = LL_DAC_IsActiveFlag_DMAUDR1(dacHandle);
			enabled = LL_DAC_IsEnabledIT_DMAUDR1(dacHandle);
		} else {
			active = LL_DAC_IsActiveFlag_DMAUDR2(dacHandle);
			enabled = LL_DAC_IsEnabledIT_DMAUDR2(dacHandle);
		}

		if (!active || !enabled) {
			return;
		}

		CallError(Error::DmaUnderrun);

		if (dacChannel == 1) {
			LL_DAC_ClearFlag_DMAUDR1(dacHandle);
		} else {
			LL_DAC_ClearFlag_DMAUDR2(dacHandle);
		}
	}


	inline void EnableIT_DmaUnderrun() {
		if (dacChannel == 1) {
			LL_DAC_EnableIT_DMAUDR1(dacHandle);
		} else {
			LL_DAC_EnableIT_DMAUDR2(dacHandle);
		}
	}


	inline void DisableIT_DmaUnderrun() {
		if (dacChannel == 1) {
			LL_DAC_DisableIT_DMAUDR1(dacHandle);
		} else {
			LL_DAC_DisableIT_DMAUDR2(dacHandle);
		}
	}


	inline void ClearFlag_DmaUnderrun() {
		if (dacChannel == 1) {
			LL_DAC_ClearFlag_DMAUDR1(dacHandle);
		} else {
			LL_DAC_ClearFlag_DMAUDR2(dacHandle);
		}
	}


	virtual ResultStatus Initialization() override {
		auto status = BeforeInitialization();
		if (status != ResultStatus::ok) {
			return status;
		}

		LL_DAC_InitTypeDef init = {
			.TriggerSource = parameters.triggerSource.Get() != 0
				? parameters.triggerSource.Get()
				: LL_DAC_TRIG_SOFTWARE,
			.TriggerSource2 = LL_DAC_TRIG_SOFTWARE,
			.WaveAutoGeneration = LL_DAC_WAVE_AUTO_GENERATION_NONE,
			.WaveAutoGenerationConfig = LL_DAC_NOISE_LFSR_UNMASK_BIT0,
			.OutputBuffer = CastOutputBuffer(),
			.OutputConnection = CastOutputConnection(),
			.OutputMode = CastOutputMode()
		};

		LL_DAC_SetSignedFormat(dacHandle, CastChannel(), LL_DAC_SIGNED_FORMAT_DISABLE);
		SystemAssert(LL_DAC_Init(dacHandle, CastChannel(), &init) == ErrorStatus::SUCCESS);
		LL_DAC_DisableDMADoubleDataMode(dacHandle, CastChannel());

		return AfterInitialization();
	}


private:
	// The DMA must carry the request of this channel, move 16-bit items in the memory-to-peripheral direction.
	ResultStatus CheckDma() const {
		return dma->CheckPeripheral(
			DmaRequest(),
			ADMA::Direction::MemoryToPeripheral,
			ADMA::DataWidth::B16
		);
	}


	// A request no DMAMUX entry carries, so the check refuses a DAC instance that has no entry in this table.
	uint32 DmaRequest() const {
		if (dacHandle == DAC1) {
			return dacChannel == 1
				? ADMA::Peripheral::Dac1Channel1.Get()
				: ADMA::Peripheral::Dac1Channel2.Get();
		}

#if defined(DAC2)
		if (dacHandle == DAC2 && dacChannel == 1) {
			return ADMA::Peripheral::Dac2Channel1.Get();
		}
#endif

#if defined(DAC3)
		if (dacHandle == DAC3) {
			return dacChannel == 1
				? ADMA::Peripheral::Dac3Channel1.Get()
				: ADMA::Peripheral::Dac3Channel2.Get();
		}
#endif

#if defined(DAC4)
		if (dacHandle == DAC4) {
			return dacChannel == 1
				? ADMA::Peripheral::Dac4Channel1.Get()
				: ADMA::Peripheral::Dac4Channel2.Get();
		}
#endif

		return 0xFFFFFFFFu;
	}


	constexpr uint32 CastChannel() const {
		switch (dacChannel) {
			case 1: return LL_DAC_CHANNEL_1;
			case 2: return LL_DAC_CHANNEL_2;
		}
		SystemAbort();
		return 0;
	}


	constexpr uint32 CastOutputBuffer() const {
		switch (parameters.outputBuffer) {
			case OutputBuffer::Enable: return LL_DAC_OUTPUT_BUFFER_ENABLE;
			case OutputBuffer::Disable: return LL_DAC_OUTPUT_BUFFER_DISABLE;
		}
		SystemAbort();
		return 0;
	}


	constexpr uint32 CastOutputConnection() const {
		switch (parameters.outputConnection) {
			case OutputConnection::External: return LL_DAC_OUTPUT_CONNECT_GPIO;
			case OutputConnection::Internal: return LL_DAC_OUTPUT_CONNECT_INTERNAL;
		}
		SystemAbort();
		return 0;
	}


	constexpr uint32 CastOutputMode() const {
		switch (parameters.outputMode) {
			case OutputMode::Normal: return LL_DAC_OUTPUT_MODE_NORMAL;
			case OutputMode::SampleHold: return LL_DAC_OUTPUT_MODE_SAMPLE_AND_HOLD;
		}
		SystemAbort();
		return 0;
	}
};
