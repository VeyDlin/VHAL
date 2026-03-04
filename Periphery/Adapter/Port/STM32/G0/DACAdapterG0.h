#pragma once
#include <System/System.h>
#include <Adapter/DACAdapter.h>
#include "DMAAdapterG0.h"


using ADAC = class DACAdapterG0;


class DACAdapterG0: public DACAdapter<DAC_TypeDef, ADMA> {
public:
	struct TriggerSource {
		static inline constexpr TriggerSourceOption Software       { LL_DAC_TRIG_SOFTWARE };
		static inline constexpr TriggerSourceOption Timer1Trigger  { LL_DAC_TRIG_EXT_TIM1_TRGO };
#if defined(TIM2)
		static inline constexpr TriggerSourceOption Timer2Trigger  { LL_DAC_TRIG_EXT_TIM2_TRGO };
#endif
		static inline constexpr TriggerSourceOption Timer3Trigger  { LL_DAC_TRIG_EXT_TIM3_TRGO };
#if defined(TIM6)
		static inline constexpr TriggerSourceOption Timer6Trigger  { LL_DAC_TRIG_EXT_TIM6_TRGO };
#endif
#if defined(TIM7)
		static inline constexpr TriggerSourceOption Timer7Trigger  { LL_DAC_TRIG_EXT_TIM7_TRGO };
#endif
#if defined(TIM15)
		static inline constexpr TriggerSourceOption Timer15Trigger { LL_DAC_TRIG_EXT_TIM15_TRGO };
#endif
		static inline constexpr TriggerSourceOption InterruptLine9 { LL_DAC_TRIG_EXT_EXTI_LINE9 };
	};




public:
	DACAdapterG0() { }

	DACAdapterG0(DAC_TypeDef *dac, uint8 channel):DACAdapter(dac, channel) {

	}




	virtual inline void IrqHandler() override {
		DmaUnderrunInterrupt();
	}




	virtual Status::statusType Write(uint16 val) override {
		LL_DAC_ConvertData12RightAligned(dacHandle, CastChannel(), val);
		return Status::ok;
	}





	virtual Status::statusType Enable() override {
		if (LL_DAC_IsEnabled(dacHandle, CastChannel())) {
			return Status::ok;
		}

		LL_DAC_Enable(dacHandle, CastChannel());
		System::DelayUs(LL_DAC_DELAY_STARTUP_VOLTAGE_SETTLING_US);
		return Status::ok;
	}





	virtual Status::statusType Disable() override {
		if (LL_DAC_IsEnabled(dacHandle, CastChannel())) {
			LL_DAC_Disable(dacHandle, CastChannel());
		}
		return Status::ok;
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


	virtual Status::statusType WriteContinuous(uint16 *buffer, uint32 count) override {
		if (dma == nullptr) {
			return Status::notAvailable;
		}
		auto status = dma->Start(buffer, (uint16*)GetDataRegisterAddress(), count);
		if (status != Status::ok) {
			return status;
		}
		EnableDMARequest();
		EnableTrigger();
		EnableIT_DmaUnderrun();
		return Status::ok;
	}


	virtual Status::statusType StopDMA() override {
		if (dma == nullptr) {
			return Status::notAvailable;
		}
		DisableIT_DmaUnderrun();
		DisableDMARequest();
		DisableTrigger();
		return dma->Stop();
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


	virtual Status::statusType Initialization() override {
		auto status = BeforeInitialization();
		if(status != Status::ok) {
			return status;
		}

		LL_DAC_InitTypeDef init = {
			.TriggerSource = parameters.triggerSource.Get() != 0
			? parameters.triggerSource.Get()
			: LL_DAC_TRIG_SOFTWARE,
			.WaveAutoGeneration = LL_DAC_WAVE_AUTO_GENERATION_NONE,
			.WaveAutoGenerationConfig = LL_DAC_NOISE_LFSR_UNMASK_BIT0,
			.OutputBuffer = CastOutputBuffer()
		};

		SystemAssert(LL_DAC_Init(dacHandle, CastChannel(), &init) == ErrorStatus::SUCCESS);

		return AfterInitialization();
	}


	virtual uint32 GetDataRegisterAddress() override {
		return LL_DAC_DMA_GetRegAddr(dacHandle, CastChannel(),
			LL_DAC_DMA_REG_DATA_12BITS_RIGHT_ALIGNED);
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


	constexpr uint32 CastOutputBuffer() const {
		switch (parameters.outputBuffer) {
			case OutputBuffer::Enable: return LL_DAC_OUTPUT_BUFFER_ENABLE;
			case OutputBuffer::Disable: return LL_DAC_OUTPUT_BUFFER_DISABLE;
		}
		SystemAbort();
		return 0;
	}
};