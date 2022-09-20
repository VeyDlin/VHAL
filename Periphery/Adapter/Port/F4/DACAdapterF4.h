 #pragma once
#include "../../DACAdapter.h"

#include <System/System.h>


using ADAC = class DACAdapterF4;


class DACAdapterF4: public DACAdapter {
public:
	DACAdapterF4() { }

	DACAdapterF4(DAC_TypeDef *dac, uint8 channel):DACAdapter(dac, channel) {

	}

	virtual inline void IrqHandler() override {

	}




	virtual Status::statusType Write(uint16 val) override {
		LL_DAC_ConvertData12RightAligned(dacHandle, CastChannel(), val);
		return Status::ok;
	};



protected:
	virtual Status::statusType Initialization() override {
		auto status = BeforeInitialization();
		if(status != Status::ok) {
			return status;
		}

		LL_DAC_InitTypeDef init = {
			.TriggerSource = LL_DAC_TRIG_SOFTWARE,
			.WaveAutoGeneration = LL_DAC_WAVE_AUTO_GENERATION_NONE,
			.WaveAutoGenerationConfig = LL_DAC_NOISE_LFSR_UNMASK_BIT0,
			.OutputBuffer = CastOutputBuffer()
		};

		SystemAssert(LL_DAC_Init(dacHandle, CastChannel(), &init) == ErrorStatus::SUCCESS);

		LL_DAC_Enable(dacHandle, CastChannel());

		return AfterInitialization();
	}





private:
	uint32 CastChannel() {
		switch (dacChannel) {
			case 1: return LL_DAC_CHANNEL_1;
			case 2: return LL_DAC_CHANNEL_2;
		}
		SystemAbort();
		return 0;
	}


	uint32 CastOutputBuffer() {
		switch (parameters.outputBuffer) {
			case OutputBuffer::Enable: return LL_DAC_OUTPUT_BUFFER_ENABLE;
			case OutputBuffer::Disable: return LL_DAC_OUTPUT_BUFFER_DISABLE;
		}
		SystemAbort();
		return 0;
	}
};















