#pragma once
#include <Adapter/DACAdapter.h>


using ADAC = class DACAdapterF4;


// DAC on the F4 peripheral, one channel per adapter. The BSP enables the DAC clock, configures the pin in analog mode and
// forwards DAC_IRQn (or TIM6_DAC_IRQn on the chips that share it) to IrqHandler().
//
// Trigger: a triggerSource that is not set starts on software. A named TriggerSource is written as its register code, and
// TriggerSource::Timer6Trigger is that code zero, so it is not the same as no choice.
//
// Not supported: continuous output by DMA (WriteContinuous and StopDMA). The F4 channels have a DMA request, but the
// stream and channel table of the DMA contract is not in the repository yet.


class DACAdapterF4: public DACAdapter<DAC_TypeDef> {
private:
	constexpr static uint32 explicitSourceFlag = 0x80000000U;



public:
	// The named sources carry explicitSourceFlag. The register code of Timer 6 is zero, which an option left unset also has, so
	// the flag tells the two apart: an unset option starts on software.
	struct TriggerSource {
		static inline constexpr TriggerSourceOption Software        { LL_DAC_TRIG_SOFTWARE | explicitSourceFlag };
		static inline constexpr TriggerSourceOption Timer2Trigger   { LL_DAC_TRIG_EXT_TIM2_TRGO | explicitSourceFlag };
		static inline constexpr TriggerSourceOption Timer4Trigger   { LL_DAC_TRIG_EXT_TIM4_TRGO | explicitSourceFlag };
		static inline constexpr TriggerSourceOption Timer5Trigger   { LL_DAC_TRIG_EXT_TIM5_TRGO | explicitSourceFlag };
		static inline constexpr TriggerSourceOption Timer6Trigger   { LL_DAC_TRIG_EXT_TIM6_TRGO | explicitSourceFlag };
		static inline constexpr TriggerSourceOption Timer7Trigger   { LL_DAC_TRIG_EXT_TIM7_TRGO | explicitSourceFlag };
		static inline constexpr TriggerSourceOption Timer8Trigger   { LL_DAC_TRIG_EXT_TIM8_TRGO | explicitSourceFlag };
		static inline constexpr TriggerSourceOption InterruptLine9  { LL_DAC_TRIG_EXT_EXTI_LINE9 | explicitSourceFlag };
	};



public:
	DACAdapterF4() { }

	DACAdapterF4(DAC_TypeDef *dac, uint8 channel):DACAdapter(dac, channel) {

	}



	// The channel is enabled only when the configuration was accepted. The base class enables it either way.
	virtual void SetParameters(Parameters val) override {
		parameters = val;

		if (Initialization() == ResultStatus::ok) {
			Enable();
		}
	}



	// The DMA underrun flag is reported whether or not its interrupt is enabled. This adapter does not enable it, so the flag
	// is raised only when the application runs the DAC by DMA itself.
	virtual inline void IrqHandler() override {
		if (dacChannel == 1) {
			if (LL_DAC_IsActiveFlag_DMAUDR1(dacHandle)) {
				LL_DAC_ClearFlag_DMAUDR1(dacHandle);
				CallError(Error::DmaUnderrun);
			}
		} else {
			if (LL_DAC_IsActiveFlag_DMAUDR2(dacHandle)) {
				LL_DAC_ClearFlag_DMAUDR2(dacHandle);
				CallError(Error::DmaUnderrun);
			}
		}
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





	virtual ResultStatus WriteContinuous(uint16 *, uint32) override {
		return ResultStatus::notSupported;
	}





	virtual ResultStatus StopDMA() override {
		return ResultStatus::notSupported;
	}





	virtual uint32 GetDataRegisterAddress() override {
		return LL_DAC_DMA_GetRegAddr(dacHandle, CastChannel(), LL_DAC_DMA_REG_DATA_12BITS_RIGHT_ALIGNED);
	}





protected:
	virtual ResultStatus Initialization() override {
		ResultStatus status = BeforeInitialization();
		if(status != ResultStatus::ok) {
			return status;
		}

		LL_DAC_InitTypeDef init = {
			.TriggerSource = CastTriggerSource(),
			.WaveAutoGeneration = LL_DAC_WAVE_AUTO_GENERATION_NONE,
			.WaveAutoGenerationConfig = LL_DAC_NOISE_LFSR_UNMASK_BIT0,
			.OutputBuffer = CastOutputBuffer()
		};

		SystemAssert(LL_DAC_Init(dacHandle, CastChannel(), &init) == ErrorStatus::SUCCESS);

		LL_DAC_Enable(dacHandle, CastChannel());
		System::DelayUs(LL_DAC_DELAY_STARTUP_VOLTAGE_SETTLING_US);

		return AfterInitialization();
	}




private:
	constexpr uint32 CastChannel() const {
		switch (dacChannel) {
			case 1: return LL_DAC_CHANNEL_1;
			case 2: return LL_DAC_CHANNEL_2;
		}
		SystemAbort();
		return 0;
	}


	// An option with no explicit source starts on software. A named source is written as its register code.
	constexpr uint32 CastTriggerSource() const {
		uint32 source = parameters.triggerSource.Get();

		if ((source & explicitSourceFlag) == 0) {
			return LL_DAC_TRIG_SOFTWARE;
		}

		return source & ~explicitSourceFlag;
	}


	constexpr uint32 CastOutputBuffer() const {
		switch (parameters.outputBuffer) {
			case OutputBuffer::Enable: return LL_DAC_OUTPUT_BUFFER_ENABLE;
			case OutputBuffer::Disable: return LL_DAC_OUTPUT_BUFFER_DISABLE;
		}
		SystemAbort();
		return 0;
	}
};
