 #pragma once
#include "../../ADCAdapter.h"
#include <System/System.h>


using AADC = class ADCAdapterG4;


class ADCAdapterG4: public ADCAdapter {
protected:
	constexpr static uint32 stabilizationTime = 3;


public:
	struct TriggerSource {
	    AUNIQUECODE_GENERATE(CTriggerSource, Software,                     LL_ADC_REG_TRIG_SOFTWARE);
	    AUNIQUECODE_GENERATE(CTriggerSource, Timer1Trigger,                LL_ADC_REG_TRIG_EXT_TIM1_TRGO);
	    AUNIQUECODE_GENERATE(CTriggerSource, Timer1Trigger2,               LL_ADC_REG_TRIG_EXT_TIM1_TRGO2);
	    AUNIQUECODE_GENERATE(CTriggerSource, Timer1Channel1,               LL_ADC_REG_TRIG_EXT_TIM1_CH1);
	    AUNIQUECODE_GENERATE(CTriggerSource, Timer1Channel2,               LL_ADC_REG_TRIG_EXT_TIM1_CH2);
	    AUNIQUECODE_GENERATE(CTriggerSource, Timer1Channel3,               LL_ADC_REG_TRIG_EXT_TIM1_CH3);
	    AUNIQUECODE_GENERATE(CTriggerSource, Timer2Trigger,                LL_ADC_REG_TRIG_EXT_TIM2_TRGO);
	    AUNIQUECODE_GENERATE(CTriggerSource, Timer2Channel1,               LL_ADC_REG_TRIG_EXT_TIM2_CH1);
	    AUNIQUECODE_GENERATE(CTriggerSource, Timer2Channel2,               LL_ADC_REG_TRIG_EXT_TIM2_CH2);
	    AUNIQUECODE_GENERATE(CTriggerSource, Timer2Channel3,               LL_ADC_REG_TRIG_EXT_TIM2_CH3);
	    AUNIQUECODE_GENERATE(CTriggerSource, Timer3Trigger,                LL_ADC_REG_TRIG_EXT_TIM3_TRGO);
	    AUNIQUECODE_GENERATE(CTriggerSource, Timer3Channel1,               LL_ADC_REG_TRIG_EXT_TIM3_CH1);
	    AUNIQUECODE_GENERATE(CTriggerSource, Timer3Channel4,               LL_ADC_REG_TRIG_EXT_TIM3_CH4);
	    AUNIQUECODE_GENERATE(CTriggerSource, Timer4Trigger,                LL_ADC_REG_TRIG_EXT_TIM4_TRGO);
	    AUNIQUECODE_GENERATE(CTriggerSource, Timer4Channel1,               LL_ADC_REG_TRIG_EXT_TIM4_CH1);
	    AUNIQUECODE_GENERATE(CTriggerSource, Timer4Channel4,               LL_ADC_REG_TRIG_EXT_TIM4_CH4);
	    AUNIQUECODE_GENERATE(CTriggerSource, Timer6Trigger,                LL_ADC_REG_TRIG_EXT_TIM6_TRGO);
	    AUNIQUECODE_GENERATE(CTriggerSource, Timer7Trigger,                LL_ADC_REG_TRIG_EXT_TIM7_TRGO);
	    AUNIQUECODE_GENERATE(CTriggerSource, Timer8Trigger,                LL_ADC_REG_TRIG_EXT_TIM8_TRGO);
	    AUNIQUECODE_GENERATE(CTriggerSource, Timer8Trigger2,               LL_ADC_REG_TRIG_EXT_TIM8_TRGO2);
	    AUNIQUECODE_GENERATE(CTriggerSource, Timer8Channel1,               LL_ADC_REG_TRIG_EXT_TIM8_CH1);
	    AUNIQUECODE_GENERATE(CTriggerSource, Timer15Trigger,               LL_ADC_REG_TRIG_EXT_TIM15_TRGO);
	    AUNIQUECODE_GENERATE(CTriggerSource, Timer20Trigger,               LL_ADC_REG_TRIG_EXT_TIM20_TRGO);
	    AUNIQUECODE_GENERATE(CTriggerSource, Timer20Trigger2,              LL_ADC_REG_TRIG_EXT_TIM20_TRGO2);
	    AUNIQUECODE_GENERATE(CTriggerSource, Timer20Channel1,              LL_ADC_REG_TRIG_EXT_TIM20_CH1);
	    AUNIQUECODE_GENERATE(CTriggerSource, Timer20Channel2,              LL_ADC_REG_TRIG_EXT_TIM20_CH2);
	    AUNIQUECODE_GENERATE(CTriggerSource, Timer20Channel3,              LL_ADC_REG_TRIG_EXT_TIM20_CH3);
	    AUNIQUECODE_GENERATE(CTriggerSource, HrtimTrigger1,                LL_ADC_REG_TRIG_EXT_HRTIM_TRG1);
	    AUNIQUECODE_GENERATE(CTriggerSource, HrtimTrigger2,                LL_ADC_REG_TRIG_EXT_HRTIM_TRG2);
	    AUNIQUECODE_GENERATE(CTriggerSource, HrtimTrigger3,                LL_ADC_REG_TRIG_EXT_HRTIM_TRG3);
	    AUNIQUECODE_GENERATE(CTriggerSource, HrtimTrigger4,                LL_ADC_REG_TRIG_EXT_HRTIM_TRG4);
	    AUNIQUECODE_GENERATE(CTriggerSource, HrtimTrigger5,                LL_ADC_REG_TRIG_EXT_HRTIM_TRG5);
	    AUNIQUECODE_GENERATE(CTriggerSource, HrtimTrigger6,                LL_ADC_REG_TRIG_EXT_HRTIM_TRG6);
	    AUNIQUECODE_GENERATE(CTriggerSource, HrtimTrigger7,                LL_ADC_REG_TRIG_EXT_HRTIM_TRG7);
	    AUNIQUECODE_GENERATE(CTriggerSource, HrtimTrigger8,                LL_ADC_REG_TRIG_EXT_HRTIM_TRG8);
	    AUNIQUECODE_GENERATE(CTriggerSource, HrtimTrigger9,                LL_ADC_REG_TRIG_EXT_HRTIM_TRG9);
	    AUNIQUECODE_GENERATE(CTriggerSource, HrtimTrigger10,               LL_ADC_REG_TRIG_EXT_HRTIM_TRG10);
	    AUNIQUECODE_GENERATE(CTriggerSource, ExtiLine11,                   LL_ADC_REG_TRIG_EXT_EXTI_LINE11);
	    AUNIQUECODE_GENERATE(CTriggerSource, ExtiLine2,                    LL_ADC_REG_TRIG_EXT_EXTI_LINE2);
	    AUNIQUECODE_GENERATE(CTriggerSource, LptimOut,                     LL_ADC_REG_TRIG_EXT_LPTIM_OUT);
	};




public:
	ADCAdapterG4() { }
	ADCAdapterG4(ADC_TypeDef *adc):ADCAdapter(adc) { }



	virtual inline void IrqHandler() override {
		ConversionInterrupt();
		InjectedInterrupt();
		WatchDogInterrupt();
		ErrorInterrupt();
	}


	virtual void AbortRegular() override {
        LL_ADC_DisableIT_EOC(adcHandle);
		LL_ADC_DisableIT_OVR(adcHandle);
		state = Status::ready;
	}


	virtual void AbortInjected() override {
		LL_ADC_ClearFlag_JEOS(adcHandle);
		LL_ADC_DisableIT_OVR(adcHandle);
		state = Status::ready;
	}


	virtual void AbortWatchDog() override {
		LL_ADC_ClearFlag_AWD1(adcHandle);
		LL_ADC_DisableIT_OVR(adcHandle);
		state = Status::ready;
	}


	virtual void AbortSampling() override { }

	virtual void AbortConfigurationReady() override { }


	virtual Status::statusType Calibration() override {
	    if (LL_ADC_IsEnabled(adcHandle)) {
	        return Status::notAvailable;
	    }

	    LL_ADC_StartCalibration(adcHandle, LL_ADC_SINGLE_ENDED);

	    while (LL_ADC_IsCalibrationOnGoing(adcHandle));

	    if (LL_ADC_IsCalibrationOnGoing(adcHandle)) {
	        return Status::error;
	    }

	    return Status::ok;
	}



protected:
	inline void ConversionInterrupt() {
        if (!LL_ADC_IsActiveFlag_EOC(adcHandle) || !LL_ADC_IsEnabledIT_EOC(adcHandle)) {
            return;
        }
        LL_ADC_ClearFlag_EOC(adcHandle);

		lastData = ReadConversionData();

		switch (GetResolutionByte()) {
			case 1: *(uint8 *)dataPointer = static_cast<uint8>(lastData);  break;
			case 2: *(uint16*)dataPointer = static_cast<uint16>(lastData); break;
			default: abort();
		}
		dataPointer += GetResolutionByte();

		if (++dataCounter < dataNeed) {
			return;
		}

		if (regularParameters.continuousMode == ContinuousMode::Single) {
			LL_ADC_DisableIT_EOC(adcHandle);
			LL_ADC_DisableIT_OVR(adcHandle);
			dataNeed = 0;
		}

		dataCounter = 0;
		dataPointer = dataPointerOriginal;
		state = Status::ready;

		CallInterrupt(Irq::Conversion);
	}





	inline void InjectedInterrupt() {
		if (!LL_ADC_IsActiveFlag_JEOS(adcHandle) || !LL_ADC_IsEnabledIT_JEOS(adcHandle)) {
			return;
		}

		if (
	        LL_ADC_INJ_IsTriggerSourceSWStart(adcHandle) &&
	        LL_ADC_INJ_GetTrigAuto(adcHandle) == LL_ADC_INJ_TRIG_INDEPENDENT &&
	        LL_ADC_REG_IsTriggerSourceSWStart(adcHandle) &&
	        injectedParameters.continuousMode == ContinuousMode::Single
		) {
			LL_ADC_DisableIT_JEOS(adcHandle);
		}

		LL_ADC_ClearFlag_JEOS(adcHandle);

		lastData = ReadConversionData();
		state = Status::ready;

		CallInterrupt(Irq::Injected);
	}





	inline void WatchDogInterrupt() {
		if (!LL_ADC_IsActiveFlag_AWD1(adcHandle) || !LL_ADC_IsEnabledIT_AWD1(adcHandle)) {
			return;
		}
		LL_ADC_ClearFlag_AWD1(adcHandle);

		state = Status::ready;

		CallInterrupt(Irq::Watchdog);
	}





	inline void ErrorInterrupt() {
		if (!LL_ADC_IsActiveFlag_OVR(adcHandle) || !LL_ADC_IsEnabledIT_OVR(adcHandle)) {
			return;
		}
		LL_ADC_ClearFlag_OVR(adcHandle);

		CallError(Error::Overrun);
	}





	inline uint16 ReadConversionData() {
		return LL_ADC_REG_ReadConversionData12(adcHandle);
	}





protected:
	virtual Status::statusType Initialization() override {
		auto status = BeforeInitialization();
		if(status != Status::ok) {
			return status;
		}

		LL_ADC_InitTypeDef initDef = {
			.Resolution = CastResolution(),
			.DataAlignment = CastDataAlignment(),
			.LowPowerMode = LL_ADC_LP_MODE_NONE
		};

		if(LL_ADC_Init(adcHandle, &initDef) != ErrorStatus::SUCCESS) {
			return Status::error;
		}

		LL_ADC_SetGainCompensation(adcHandle, 0);
		LL_ADC_SetOverSamplingScope(adcHandle, LL_ADC_OVS_DISABLE);

		LL_ADC_CommonInitTypeDef initCommon = {
			.CommonClock = LL_ADC_CLOCK_SYNC_PCLK_DIV4, // TODO: CommonClock!

			#if defined(ADC_MULTIMODE_SUPPORT)
				.Multimode = LL_ADC_MULTI_INDEPENDENT, // TODO: Multimode!
				.MultiDMATransfer = LL_ADC_MULTI_REG_DMA_EACH_ADC,
				.MultiTwoSamplingDelay = LL_ADC_MULTI_TWOSMP_DELAY_5CYCLES // TODO: MultiTwoSamplingDelay!
			#endif
		};
		if(LL_ADC_CommonInit(__LL_ADC_COMMON_INSTANCE(adcHandle), &initCommon) != ErrorStatus::SUCCESS) {
			return Status::error;
		}

		LL_ADC_DisableDeepPowerDown(adcHandle);
		LL_ADC_EnableInternalRegulator(adcHandle);

		System::DelayUs(LL_ADC_DELAY_INTERNAL_REGUL_STAB_US);

		return AfterInitialization();
	}





	virtual Status::statusType RegularInitialization(uint8 rankLength) override {
		LL_ADC_REG_InitTypeDef init = {
			.TriggerSource = regularParameters.triggerSourceCode->GetCode(),
			.SequencerLength = CastSequencerLength(rankLength),
			.SequencerDiscont = LL_ADC_REG_SEQ_DISCONT_DISABLE,
			.ContinuousMode = CastContinuousMode(regularParameters.continuousMode),
			.DMATransfer = LL_ADC_REG_DMA_TRANSFER_NONE,
			.Overrun = LL_ADC_REG_OVR_DATA_PRESERVED
		};
		if(LL_ADC_REG_Init(adcHandle, &init) != ErrorStatus::SUCCESS) {
			return Status::error;
		}

		LL_ADC_SetGainCompensation(adcHandle, 0);
		LL_ADC_SetOverSamplingScope(adcHandle, LL_ADC_OVS_DISABLE);

		return Status::ok;
	}





	virtual Status::statusType InjectedInitialization(uint8 rankLength) override {
	    LL_ADC_INJ_InitTypeDef init = {
	        .TriggerSource = injectedParameters.triggerSourceCode->GetCode(),
	        .SequencerLength = CastSequencerLength(rankLength),
	        .SequencerDiscont = LL_ADC_INJ_SEQ_DISCONT_DISABLE,
	        .TrigAuto = LL_ADC_INJ_TRIG_INDEPENDENT
	    };

	    if (LL_ADC_INJ_Init(adcHandle, &init) != ErrorStatus::SUCCESS) {
	        return Status::error;
	    }

	    LL_ADC_SetGainCompensation(adcHandle, 0);
	    LL_ADC_SetOverSamplingScope(adcHandle, LL_ADC_OVS_DISABLE);

	    return Status::ok;
	}





	virtual Status::statusType ReadByteArray(uint8* buffer, uint16 size) override {
	    if (state != Status::ready) {
	        return Status::busy;
	    }

	    if (!LL_ADC_IsEnabled(adcHandle)) {
	        LL_ADC_Enable(adcHandle);
	        System::DelayUs(stabilizationTime);
	    }

	    if (!LL_ADC_IsEnabled(adcHandle)) {
	        return Status::notAvailable;
	    }

	    state = Status::busy;
	    dataNeed = size / GetResolutionByte();
	    dataCounter = 0;
	    dataPointer = buffer;
	    uint32 tickStart = System::GetTick();

	    LL_ADC_ClearFlag_EOC(adcHandle);
	    LL_ADC_ClearFlag_EOS(adcHandle);
	    LL_ADC_ClearFlag_OVR(adcHandle);

	    LL_ADC_REG_StartConversion(adcHandle);

	    for (dataCounter = 0; dataCounter < dataNeed; dataCounter++) {
	        while (!LL_ADC_IsActiveFlag_EOC(adcHandle)) {
	            if ((System::GetTick() - tickStart) > timeout) {
	                state = Status::ready;
	                return Status::timeout;
	            }
	        }

	        lastData = ReadConversionData();

	        switch (GetResolutionByte()) {
	            case 1: *(uint8*)dataPointer = static_cast<uint8>(lastData);  break;
	            case 2: *(uint16*)dataPointer = static_cast<uint16>(lastData); break;
	            default: abort();
	        }
	        dataPointer += GetResolutionByte();
	    }

	    if (LL_ADC_REG_IsTriggerSourceSWStart(adcHandle) && regularParameters.continuousMode == ContinuousMode::Single) {
	        if (LL_ADC_IsActiveFlag_EOC(adcHandle)) {
	            if (LL_ADC_REG_IsConversionOngoing(adcHandle)) {
	                CallError(Error::AdcConfig);
	            }
	        }
	    }

	    LL_ADC_ClearFlag_EOC(adcHandle);
	    LL_ADC_ClearFlag_EOS(adcHandle);

	    state = Status::ready;
	    return Status::ok;
	}






	virtual Status::statusType ReadByteArrayAsync(uint8 *buffer, uint16 size) override {
		if (state != Status::ready) {
			return Status::busy;
		}

		if (!LL_ADC_IsEnabled(adcHandle)) {
			LL_ADC_Enable(adcHandle);
			System::DelayUs(stabilizationTime);
		}

		if (!LL_ADC_IsEnabled(adcHandle)) {
			return Status::notAvailable;
		}

		state = Status::busy;
		dataNeed = size / GetResolutionByte();
		dataCounter = 0;
		dataPointer = buffer;
		dataPointerOriginal = buffer;


		LL_ADC_ClearFlag_EOC(adcHandle);
		LL_ADC_ClearFlag_OVR(adcHandle);

		LL_ADC_EnableIT_EOC(adcHandle);
		LL_ADC_EnableIT_OVR(adcHandle);


		if (LL_ADC_REG_IsTriggerSourceSWStart(adcHandle)) {
			 LL_ADC_REG_StartConversion(adcHandle);
		}

		return Status::ok;
	}



	virtual Status::info<float> SetInjectedChannel(const InjecteChannel &channel, uint8 rank) override {
		return { Status::notSupported };
	}



	virtual Status::info<float> SetRegularChannel(const RegularChannel &channel, uint8 rank) override {
		float outSamplingTime = 0;
		uint32 samplingTime = CastSamplingTime(channel.maxSamplingCycles, outSamplingTime);

		LL_ADC_REG_SetSequencerRanks(adcHandle, CastRank(rank), CastChannel(channel.channel));
		LL_ADC_SetChannelSamplingTime(adcHandle, CastChannel(channel.channel), samplingTime);
		LL_ADC_SetChannelSingleDiff(adcHandle, CastChannel(channel.channel), LL_ADC_SINGLE_ENDED);

		return { Status::ok, outSamplingTime };
	}



private:
	uint32 CastResolution() {
		switch (parameters.resolution) {
			case Resolution::B12: return LL_ADC_RESOLUTION_12B;
			case Resolution::B10: return LL_ADC_RESOLUTION_10B;
			case Resolution::B8: return LL_ADC_RESOLUTION_8B;
			case Resolution::B6: return LL_ADC_RESOLUTION_6B;

			case Resolution::B14:
			case Resolution::B16:
				abort();
		}
		abort();
		return 0;
	}



	uint32 CastDataAlignment() {
		switch (parameters.dataAlignment) {
			case DataAlignment::Right: return LL_ADC_DATA_ALIGN_RIGHT;
			case DataAlignment::Left: return LL_ADC_DATA_ALIGN_LEFT;
		}
		abort();
		return 0;
	}





	uint32 CastContinuousMode(ContinuousMode val) {
		switch (val) {
			case ContinuousMode::Continuous: return LL_ADC_REG_CONV_CONTINUOUS;
			case ContinuousMode::Single: return LL_ADC_REG_CONV_SINGLE;
		}
		abort();
		return 0;
	}



	uint32 CastSequencerLength(uint8 val) {
		static const uint32 array[] = {
			LL_ADC_REG_SEQ_SCAN_DISABLE,
			LL_ADC_REG_SEQ_SCAN_ENABLE_2RANKS,  LL_ADC_REG_SEQ_SCAN_ENABLE_3RANKS,
			LL_ADC_REG_SEQ_SCAN_ENABLE_4RANKS,  LL_ADC_REG_SEQ_SCAN_ENABLE_5RANKS,
			LL_ADC_REG_SEQ_SCAN_ENABLE_6RANKS,  LL_ADC_REG_SEQ_SCAN_ENABLE_7RANKS,
			LL_ADC_REG_SEQ_SCAN_ENABLE_8RANKS,  LL_ADC_REG_SEQ_SCAN_ENABLE_9RANKS,
			LL_ADC_REG_SEQ_SCAN_ENABLE_10RANKS, LL_ADC_REG_SEQ_SCAN_ENABLE_11RANKS,
			LL_ADC_REG_SEQ_SCAN_ENABLE_12RANKS, LL_ADC_REG_SEQ_SCAN_ENABLE_13RANKS,
			LL_ADC_REG_SEQ_SCAN_ENABLE_14RANKS, LL_ADC_REG_SEQ_SCAN_ENABLE_15RANKS,
			LL_ADC_REG_SEQ_SCAN_ENABLE_16RANKS,
		};
		SystemAssert(val <= 16 && val >= 1);
		return array[val - 1];
	}



	uint32 CastRank(uint8 val) {
		static const uint32 array[] = {
			LL_ADC_REG_RANK_1,  LL_ADC_REG_RANK_2,  LL_ADC_REG_RANK_3,  LL_ADC_REG_RANK_4,
			LL_ADC_REG_RANK_5,  LL_ADC_REG_RANK_6,  LL_ADC_REG_RANK_7,  LL_ADC_REG_RANK_8,
			LL_ADC_REG_RANK_9,  LL_ADC_REG_RANK_10, LL_ADC_REG_RANK_11, LL_ADC_REG_RANK_12,
			LL_ADC_REG_RANK_13, LL_ADC_REG_RANK_14, LL_ADC_REG_RANK_15, LL_ADC_REG_RANK_16
		};
		SystemAssert(val <= 16 && val >= 1);
		return array[val - 1];
	}



	uint32 CastChannel(uint8 val) {
		static const uint32 array[] = {
			LL_ADC_CHANNEL_0, LL_ADC_CHANNEL_1,  LL_ADC_CHANNEL_2,  LL_ADC_CHANNEL_3,
			LL_ADC_CHANNEL_4, LL_ADC_CHANNEL_5,  LL_ADC_CHANNEL_6,  LL_ADC_CHANNEL_7,
			LL_ADC_CHANNEL_8, LL_ADC_CHANNEL_9,  LL_ADC_CHANNEL_10, LL_ADC_CHANNEL_11,
			LL_ADC_CHANNEL_12, LL_ADC_CHANNEL_13, LL_ADC_CHANNEL_14, LL_ADC_CHANNEL_15,
			LL_ADC_CHANNEL_16, LL_ADC_CHANNEL_17, LL_ADC_CHANNEL_18
		};
		SystemAssert(val <= 18 && val >= 0);
		return array[val];
	}



	uint32 CastSamplingTime(uint16 maxSamplingCycles, float &outSamplingTime) {
		static const uint8 arraySize = 8;
		struct SamplingElement { uint32 reg; float val; };
		static const SamplingElement samplingArray[arraySize] = {
			{ LL_ADC_SAMPLINGTIME_2CYCLES_5, 	2.5   },
			{ LL_ADC_SAMPLINGTIME_6CYCLES_5,	6.5   },
			{ LL_ADC_SAMPLINGTIME_12CYCLES_5, 	12.5  },
			{ LL_ADC_SAMPLINGTIME_24CYCLES_5, 	24.5  },
			{ LL_ADC_SAMPLINGTIME_47CYCLES_5, 	47.5  },
			{ LL_ADC_SAMPLINGTIME_92CYCLES_5,	92.5  },
			{ LL_ADC_SAMPLINGTIME_247CYCLES_5, 	247.5 },
			{ LL_ADC_SAMPLINGTIME_640CYCLES_5,	640.5 }
		};


		if(maxSamplingCycles == 0) {
			outSamplingTime = samplingArray[0].val;
			return samplingArray[0].reg;
		}

		for (uint8 i = 0; i < arraySize; i++) {
			if(maxSamplingCycles <= samplingArray[i].val) {
				outSamplingTime = samplingArray[i].val;
				return samplingArray[i].reg;
			}
		}

		outSamplingTime = samplingArray[arraySize - 1].val;
		return samplingArray[arraySize - 1].reg;
	}
};
