 #pragma once
#include "../../ADCAdapter.h"
#include <System/System.h>


using AADC = class ADCAdapterG0;


class ADCAdapterG0: public ADCAdapter {
protected:
	constexpr static uint32 stabilizationTime = 3;
	constexpr static uint32 tempSensorStabilizationTime = 3;


public:
	struct TriggerSource {
		static inline constexpr TriggerSourceOption Software        { LL_ADC_REG_TRIG_SOFTWARE };
		static inline constexpr TriggerSourceOption Timer1Trigger2  { LL_ADC_REG_TRIG_EXT_TIM1_TRGO2 };
		static inline constexpr TriggerSourceOption Timer1Channel4  { LL_ADC_REG_TRIG_EXT_TIM1_CH4 };
	#if defined(TIM2)
		static inline constexpr TriggerSourceOption Timer2Trigger   { LL_ADC_REG_TRIG_EXT_TIM2_TRGO };
	#endif
		static inline constexpr TriggerSourceOption Timer3Trigger   { LL_ADC_REG_TRIG_EXT_TIM3_TRGO };
	#if defined(TIM4)
		static inline constexpr TriggerSourceOption Timer4Trigger   { LL_ADC_REG_TRIG_EXT_TIM4_TRGO };
	#endif
	#if defined(TIM6)
		static inline constexpr TriggerSourceOption Timer6Trigger   { LL_ADC_REG_TRIG_EXT_TIM6_TRGO };
	#endif
	#if defined(TIM15)
		static inline constexpr TriggerSourceOption Timer15Trigger  { LL_ADC_REG_TRIG_EXT_TIM15_TRGO };
	#endif
		static inline constexpr TriggerSourceOption InterruptLine11 { LL_ADC_REG_TRIG_EXT_EXTI_LINE11 };
	};




public:
	ADCAdapterG0() { }
	ADCAdapterG0(ADC_TypeDef *adc):ADCAdapter(adc) { }



	virtual inline void IrqHandler() override {
		EndSamplingInterrupt();
		ConversionInterrupt();
		WatchDogInterrupt();
		ErrorInterrupt();
		ConfigurationReady();
	}


	virtual void AbortRegular() override {
		LL_ADC_DisableIT_EOC(adcHandle);
		LL_ADC_DisableIT_EOS(adcHandle);
		LL_ADC_DisableIT_OVR(adcHandle);
		state = Status::ready;
	}


	virtual void AbortInjected() override { }


	virtual void AbortWatchDog() override {
		LL_ADC_ClearFlag_AWD1(adcHandle);
		LL_ADC_ClearFlag_AWD2(adcHandle);
		LL_ADC_ClearFlag_AWD3(adcHandle);
		LL_ADC_DisableIT_OVR(adcHandle);
		state = Status::ready;
	}


	virtual void AbortSampling() override {

		state = Status::ready;
	}


	virtual void AbortConfigurationReady() override {

		state = Status::ready;
	}





	virtual Status::statusType Calibration() override {
		if (state != Status::ready) {
			return Status::busy;
		}
		state = Status::busy;


		DisableCorrect();

		if (LL_ADC_IsEnabled(adcHandle)) {
			return Status::notAvailable;
		}


		auto backupLowPowerMode = LL_ADC_GetLowPowerMode(adcHandle);
		auto backupDmaTransfer = LL_ADC_REG_GetDMATransfer(adcHandle);

		LL_ADC_SetLowPowerMode(adcHandle, LL_ADC_LP_MODE_NONE);
		LL_ADC_REG_SetDMATransfer(adcHandle, LL_ADC_REG_DMA_TRANSFER_NONE);

		uint32_t calibrationFactor = 0;
		uint32 calibrationIndex = 0;


		for (; calibrationIndex < 8; calibrationIndex++) {
			uint32 tickStart = System::GetTick();

			LL_ADC_StartCalibration(adcHandle);

			while (LL_ADC_IsCalibrationOnGoing(adcHandle) != 0UL) {
				if ((System::GetTick() - tickStart) > timeout) {

					LL_ADC_SetLowPowerMode(adcHandle, backupLowPowerMode);
					LL_ADC_REG_SetDMATransfer(adcHandle, backupDmaTransfer);
					state = Status::ready;

					return Status::timeout;
				}
			}

			calibrationFactor += LL_ADC_GetCalibrationFactor(adcHandle);
		}


		// Compute average
		calibrationFactor /= calibrationIndex;

		// Apply calibration factor
		LL_ADC_Enable(adcHandle);
		LL_ADC_SetCalibrationFactor(adcHandle, calibrationFactor);
		LL_ADC_Disable(adcHandle);


		// Wait for ADC effectively disabled before changing configuration
		uint32 tickStart = System::GetTick();

		while (LL_ADC_IsEnabled(adcHandle)) {
			if ((System::GetTick() - tickStart) > timeout) {

				// New check to avoid false timeout detection in case of preemption
				if (LL_ADC_IsEnabled(adcHandle)) {

					LL_ADC_SetLowPowerMode(adcHandle, backupLowPowerMode);
					LL_ADC_REG_SetDMATransfer(adcHandle, backupDmaTransfer);
					state = Status::ready;

					return Status::timeout;
				}
			}
		}


		LL_ADC_SetLowPowerMode(adcHandle, backupLowPowerMode);
		LL_ADC_REG_SetDMATransfer(adcHandle, backupDmaTransfer);

		state = Status::ready;

		return Status::ok;
	}





protected:
	inline void EndSamplingInterrupt() {
		if (!LL_ADC_IsActiveFlag_EOSMP(adcHandle) || !LL_ADC_IsEnabledIT_EOSMP(adcHandle)) {
			return;
		}

		CallInterrupt(Irq::Sampling);

		LL_ADC_ClearFlag_EOSMP(adcHandle);
	}





	inline void ConversionInterrupt() {
		if (
			(!LL_ADC_IsActiveFlag_EOC(adcHandle) || !LL_ADC_IsEnabledIT_EOC(adcHandle)) &&
			(!LL_ADC_IsActiveFlag_EOS(adcHandle) || !LL_ADC_IsEnabledIT_EOS(adcHandle))
		) {
			return;
		}

		lastData = ReadConversionData();

		switch (GetResolutionByte()) {
			case 1: *(uint8*)dataPointer = static_cast<uint8>(lastData);  break;
			case 2: *(uint16*)dataPointer = static_cast<uint16>(lastData); break;
			default: abort();
		}
		dataPointer += GetResolutionByte();

		if (++dataCounter < dataNeed) {
			LL_ADC_ClearFlag_EOC(adcHandle);
			LL_ADC_ClearFlag_EOS(adcHandle);
			return;
		}

		if (regularParameters.continuousMode == ContinuousMode::Single) {
			if (LL_ADC_REG_IsConversionOngoing(adcHandle)) {
				CallError(Error::AdcConfig);
			}

			LL_ADC_DisableIT_EOC(adcHandle);
			LL_ADC_DisableIT_EOS(adcHandle);
			LL_ADC_DisableIT_OVR(adcHandle);
			dataNeed = 0;
		}

		dataCounter = 0;
		dataPointer = dataPointerOriginal;
		state = Status::ready;

		CallInterrupt(Irq::Conversion);

		LL_ADC_ClearFlag_EOC(adcHandle);
		LL_ADC_ClearFlag_EOS(adcHandle);
	}





	inline void WatchDogInterrupt() {
		if (LL_ADC_IsActiveFlag_AWD1(adcHandle) && LL_ADC_IsEnabledIT_AWD1(adcHandle)) {
			LL_ADC_ClearFlag_AWD1(adcHandle);
			state = Status::ready;
			CallInterrupt(Irq::Watchdog, 1);
		}

		if (LL_ADC_IsActiveFlag_AWD2(adcHandle) && LL_ADC_IsEnabledIT_AWD2(adcHandle)) {
			LL_ADC_ClearFlag_AWD2(adcHandle);
			state = Status::ready;
			CallInterrupt(Irq::Watchdog, 1);
		}

		if (LL_ADC_IsActiveFlag_AWD3(adcHandle) && LL_ADC_IsEnabledIT_AWD3(adcHandle)) {
			LL_ADC_ClearFlag_AWD3(adcHandle);
			state = Status::ready;
			CallInterrupt(Irq::Watchdog, 1);
		}
	}





	inline void ErrorInterrupt() {
		if (!LL_ADC_IsActiveFlag_OVR(adcHandle) || !LL_ADC_IsEnabledIT_OVR(adcHandle)) {
			return;
		}

		CallError(Error::Overrun);

		LL_ADC_ClearFlag_OVR(adcHandle);
	}





	inline void ConfigurationReady() {
		if (!LL_ADC_IsActiveFlag_CCRDY(adcHandle) || !LL_ADC_IsEnabledIT_CCRDY(adcHandle)) {
			return;
		}
		CallInterrupt(Irq::ConfigReady);

		LL_ADC_ClearFlag_CCRDY(adcHandle);
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
			.Clock = LL_ADC_CLOCK_SYNC_PCLK_DIV2,
			.Resolution = CastResolution(),
			.DataAlignment = CastDataAlignment(),
			.LowPowerMode = LL_ADC_LP_MODE_NONE
		};

		if(LL_ADC_Init(adcHandle, &initDef) != ErrorStatus::SUCCESS) {
			return Status::error;
		}

		LL_ADC_REG_SetSequencerConfigurable(adcHandle, LL_ADC_REG_SEQ_CONFIGURABLE);

		// Poll for ADC channel configuration ready
		while (LL_ADC_IsActiveFlag_CCRDY(adcHandle) == 0);

		// Clear flag ADC channel configuration ready
		LL_ADC_ClearFlag_CCRDY(adcHandle);



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



		LL_ADC_SetOverSamplingScope(adcHandle, LL_ADC_OVS_DISABLE);
		LL_ADC_SetTriggerFrequencyMode(adcHandle, LL_ADC_CLOCK_FREQ_MODE_HIGH);
		LL_ADC_SetSamplingTimeCommonChannels(adcHandle, LL_ADC_SAMPLINGTIME_COMMON_1, LL_ADC_SAMPLINGTIME_1CYCLE_5); // TODO: LL_ADC_SetSamplingTimeCommonChannels
		LL_ADC_SetSamplingTimeCommonChannels(adcHandle, LL_ADC_SAMPLINGTIME_COMMON_2, LL_ADC_SAMPLINGTIME_1CYCLE_5);
		LL_ADC_DisableIT_EOC(adcHandle);
		LL_ADC_DisableIT_EOS(adcHandle);



		LL_ADC_EnableInternalRegulator(adcHandle);
		System::DelayUs(LL_ADC_DELAY_INTERNAL_REGUL_STAB_US);


		return AfterInitialization();
	}





	virtual Status::statusType RegularInitialization(uint8 rankLength) override {
		LL_ADC_REG_InitTypeDef init = {
			.TriggerSource = regularParameters.triggerSourceCode->GetCode(),
			.SequencerLength = CastSequencerLength(rankLength), // rankLength
			.SequencerDiscont = LL_ADC_REG_SEQ_DISCONT_DISABLE,
			.ContinuousMode = CastContinuousMode(regularParameters.continuousMode),
			.DMATransfer = LL_ADC_REG_DMA_TRANSFER_NONE,
			.Overrun = LL_ADC_REG_OVR_DATA_PRESERVED
		};
		if(LL_ADC_REG_Init(adcHandle, &init) != ErrorStatus::SUCCESS) {
			return Status::error;
		}

		return Status::ok;
	}





	virtual Status::statusType InjectedInitialization(uint8 rankLength) override {
		return Status::notSupported;
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
		LL_ADC_ClearFlag_EOS(adcHandle);
		LL_ADC_ClearFlag_OVR(adcHandle);

		LL_ADC_EnableIT_EOC(adcHandle);
		// LL_ADC_EnableIT_EOS(adcHandle); // // sequence conversions
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
		//LL_ADC_SetChannelSamplingTime(adcHandle, CastChannel(channel.channel), samplingTime);

		// TODO: LL_ADC_SetSamplingTimeCommonChannels
		LL_ADC_SetSamplingTimeCommonChannels(adcHandle, LL_ADC_SAMPLINGTIME_COMMON_1, samplingTime);
		LL_ADC_SetChannelSamplingTime(adcHandle, CastChannel(channel.channel), LL_ADC_SAMPLINGTIME_COMMON_1);

		return { Status::ok, outSamplingTime };
	}



private:
	constexpr uint32 CastResolution() const {
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



	constexpr uint32 CastDataAlignment() const {
		switch (parameters.dataAlignment) {
			case DataAlignment::Right: return LL_ADC_DATA_ALIGN_RIGHT;
			case DataAlignment::Left: return LL_ADC_DATA_ALIGN_LEFT;
		}
		abort();
		return 0;
	}



	constexpr uint32 CastContinuousMode(ContinuousMode val) const {
		switch (val) {
			case ContinuousMode::Continuous: return LL_ADC_REG_CONV_CONTINUOUS;
			case ContinuousMode::Single: return LL_ADC_REG_CONV_SINGLE;
		}
		abort();
		return 0;
	}



	constexpr uint32 CastSequencerLength(uint8 val) const {
		constexpr uint32 array[] = {
			LL_ADC_REG_SEQ_SCAN_DISABLE,
			LL_ADC_REG_SEQ_SCAN_ENABLE_2RANKS,  LL_ADC_REG_SEQ_SCAN_ENABLE_3RANKS,
			LL_ADC_REG_SEQ_SCAN_ENABLE_4RANKS,  LL_ADC_REG_SEQ_SCAN_ENABLE_5RANKS,
			LL_ADC_REG_SEQ_SCAN_ENABLE_6RANKS,  LL_ADC_REG_SEQ_SCAN_ENABLE_7RANKS,
			LL_ADC_REG_SEQ_SCAN_ENABLE_8RANKS,
		};
		SystemAssert(val <= 8 && val >= 1);
		return array[val - 1];
	}



	constexpr uint32 CastRank(uint8 val) const {
		constexpr uint32 array[] = {
			LL_ADC_REG_RANK_1,  LL_ADC_REG_RANK_2,  LL_ADC_REG_RANK_3,  LL_ADC_REG_RANK_4,
			LL_ADC_REG_RANK_5,  LL_ADC_REG_RANK_6,  LL_ADC_REG_RANK_7,  LL_ADC_REG_RANK_8,
		};
		SystemAssert(val <= 8 && val >= 1);
		return array[val - 1];
	}



	constexpr uint32 CastChannel(uint8 val) const {
		constexpr uint32 array[] = {
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
		static const uint8 arraySize = 7;
		struct SamplingElement { uint32 reg; float val; };
		static const SamplingElement samplingArray[arraySize] = {
			{ LL_ADC_SAMPLINGTIME_1CYCLE_5, 	1.5   },
			{ LL_ADC_SAMPLINGTIME_3CYCLES_5,	3.5   },
			{ LL_ADC_SAMPLINGTIME_7CYCLES_5, 	7.5   },
			{ LL_ADC_SAMPLINGTIME_19CYCLES_5, 	19.5  },
			{ LL_ADC_SAMPLINGTIME_39CYCLES_5, 	39.5  },
			{ LL_ADC_SAMPLINGTIME_79CYCLES_5,	79.5  },
			{ LL_ADC_SAMPLINGTIME_160CYCLES_5, 	160.5 }
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





	Status::statusType DisableCorrect() {
		if (!LL_ADC_IsEnabled(adcHandle) || LL_ADC_IsDisableOngoing(adcHandle)) {
			return Status::ok;
		}

		if (LL_ADC_REG_IsConversionOngoing(adcHandle)) {
			return Status::error;
		}

		LL_ADC_Disable(adcHandle);
		LL_ADC_ClearFlag_EOSMP(adcHandle);
		LL_ADC_ClearFlag_ADRDY(adcHandle);

		uint32 tickStart = System::GetTick();
		while (LL_ADC_IsEnabled(adcHandle)) {
			if ((System::GetTick() - tickStart) > timeout) {

				// New check to avoid false timeout detection in case of preemption
				if (LL_ADC_IsEnabled(adcHandle)) {
					return Status::error;
				}
			}
		}

		return Status::ok;
	}
};
