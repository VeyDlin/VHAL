 #pragma once
#include "../../DACAdapter.h"

#include <System/System.h>


using ADAC = class DACAdapterG4;


class DACAdapterG4: public DACAdapter {
public:
	DACAdapterG4() { }

	DACAdapterG4(DAC_TypeDef *dac, uint8 channel):DACAdapter(dac, channel) {

	}

	virtual inline void IrqHandler() override {
		// TODO: [VHAL] [DAC] [G4] [ADD SUPPORT]
	}




	virtual Status::statusType Write(uint16 val) override {
		LL_DAC_ConvertData12RightAligned(dacHandle, CastChannel(), val);
		System::DelayUs(LL_DAC_DELAY_VOLTAGE_SETTLING_US );
		return Status::ok;
	};





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



protected:
	virtual Status::statusType Initialization() override {
		auto status = BeforeInitialization();
		if(status != Status::ok) {
			return status;
		}

		LL_DAC_InitTypeDef init = {
			.TriggerSource = LL_DAC_TRIG_SOFTWARE,
			.TriggerSource2 = LL_DAC_TRIG_SOFTWARE,
			.WaveAutoGeneration = LL_DAC_WAVE_AUTO_GENERATION_NONE,
			.WaveAutoGenerationConfig = LL_DAC_NOISE_LFSR_UNMASK_BIT0,
			.OutputBuffer = CastOutputBuffer(),
			.OutputConnection = CastOutputConnection(),
			.OutputMode = CastOutputMode()
		};

		LL_DAC_SetSignedFormat(dacHandle, CastChannel(), LL_DAC_SIGNED_FORMAT_DISABLE);
		SystemAssert(LL_DAC_Init(dacHandle, CastChannel(), &init) == ErrorStatus::SUCCESS);
		LL_DAC_DisableTrigger(dacHandle, CastChannel());
		LL_DAC_DisableDMADoubleDataMode(dacHandle, CastChannel());

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















