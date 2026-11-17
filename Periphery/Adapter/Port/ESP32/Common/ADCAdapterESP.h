#pragma once
#include <Adapter/ADCAdapter.h>


using AADC = class ADCAdapterESP;


class ADCAdapterESP : public ADCAdapter<void> {
	static constexpr uint8 maxRegularChannels = 8;

public:
	struct AttenuationOption : IOption<uint32> {
		using IOption::IOption;
	};


	struct Attenuation {
		static inline constexpr AttenuationOption Db0 { ADC_ATTEN_DB_0 };
		static inline constexpr AttenuationOption Db2_5 { ADC_ATTEN_DB_2_5 };
		static inline constexpr AttenuationOption Db6 { ADC_ATTEN_DB_6 };
		static inline constexpr AttenuationOption Db12 { ADC_ATTEN_DB_12 };
	};


protected:
	adc_oneshot_unit_handle_t unitHandle = nullptr;
	adc_unit_t unit = ADC_UNIT_1;

	struct ChannelEntry {
		adc_channel_t channel;
		adc_atten_t attenuation;
		bool configured = false;
		adc_cali_handle_t calibration = nullptr;
	};
	ChannelEntry regularChannels[maxRegularChannels] = {};
	uint8 regularCount = 0;
	adc_atten_t attenuation = ADC_ATTEN_DB_12;


public:
	ADCAdapterESP() = default;

	ADCAdapterESP(adc_unit_t adcUnit)
		: unit(adcUnit) { }

	~ADCAdapterESP() {
		DeleteCalibrations();
		if (unitHandle) {
			adc_oneshot_del_unit(unitHandle);
		}
	}


	void IrqHandler() override { }

	void AbortRegular() override { state = ResultStatus::ready; }
	void AbortInjected() override { }
	void AbortWatchDog() override { }
	void AbortSampling() override { }
	void AbortConfigurationReady() override { }

	ResultStatus Calibration() override {
		if (unitHandle == nullptr) {
			return ResultStatus::noInit;
		}

		bool hasChannel = false;
		for (uint8 i = 0; i < regularCount; i++) {
			if (regularChannels[i].configured) {
				hasChannel = true;
			}
		}
		if (!hasChannel) {
			return ResultStatus::notReady;
		}

		esp_err_t err = ESP_OK;
		for (uint8 i = 0; i < regularCount; i++) {
			ChannelEntry &entry = regularChannels[i];
			if (!entry.configured) {
				continue;
			}

			DeleteCalibration(entry);

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
			adc_cali_curve_fitting_config_t caliConfig = {};
			caliConfig.unit_id = unit;
			caliConfig.chan = entry.channel;
			caliConfig.atten = entry.attenuation;
			caliConfig.bitwidth = CastBitwidth();
			err = adc_cali_create_scheme_curve_fitting(&caliConfig, &entry.calibration);
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
			adc_cali_line_fitting_config_t caliConfig = {};
			caliConfig.unit_id = unit;
			caliConfig.atten = entry.attenuation;
			caliConfig.bitwidth = CastBitwidth();
#if CONFIG_IDF_TARGET_ESP32
			// Used only by chips whose eFuse carries no calibration values; 1100 mV is the nominal reference.
			caliConfig.default_vref = 1100;
#endif
			err = adc_cali_create_scheme_line_fitting(&caliConfig, &entry.calibration);
#else
			return ResultStatus::notSupported;
#endif

			if (err == ESP_ERR_NOT_SUPPORTED) {
				return ResultStatus::notSupported;
			}
			if (err != ESP_OK) {
				return ResultStatus::error;
			}
		}

		return ResultStatus::ok;
	}


	void SetAttenuation(AttenuationOption val) {
		attenuation = static_cast<adc_atten_t>(val.Get());
	}


	Result<uint32> ReadVoltage(uint8 rank = 1) {
		if (unitHandle == nullptr) {
			return ResultStatus::noInit;
		}
		if (!IsRankConfigured(rank)) {
			return ResultStatus::outOfRange;
		}

		ChannelEntry &entry = regularChannels[rank - 1];
		if (entry.calibration == nullptr) {
			return ResultStatus::notReady;
		}

		int voltage = 0;
		if (adc_oneshot_get_calibrated_result(unitHandle, entry.calibration, entry.channel, &voltage) != ESP_OK) {
			return ResultStatus::error;
		}
		return static_cast<uint32>(voltage);
	}


	Result<uint32> RawToVoltage(uint32 raw, uint8 rank = 1) {
		if (!IsRankConfigured(rank)) {
			return ResultStatus::outOfRange;
		}

		ChannelEntry &entry = regularChannels[rank - 1];
		if (entry.calibration == nullptr) {
			return ResultStatus::notReady;
		}

		int voltage = 0;
		if (adc_cali_raw_to_voltage(entry.calibration, static_cast<int>(raw), &voltage) != ESP_OK) {
			return ResultStatus::error;
		}
		return static_cast<uint32>(voltage);
	}


protected:
	ResultStatus Initialization() override {
		auto status = BeforeInitialization();
		if (status != ResultStatus::ok) {
			return status;
		}

		uint8 bitCount = static_cast<uint8>(parameters.resolution);
		if (bitCount < SOC_ADC_RTC_MIN_BITWIDTH || bitCount > SOC_ADC_RTC_MAX_BITWIDTH) {
			return ResultStatus::notSupported;
		}

		DeleteCalibrations();
		for (uint8 i = 0; i < maxRegularChannels; i++) {
			regularChannels[i] = {};
		}
		regularCount = 0;

		if (unitHandle) {
			adc_oneshot_del_unit(unitHandle);
			unitHandle = nullptr;
		}

		adc_oneshot_unit_init_cfg_t init_config = {};
		init_config.unit_id = unit;
		init_config.ulp_mode = ADC_ULP_MODE_DISABLE;

		if (adc_oneshot_new_unit(&init_config, &unitHandle) != ESP_OK) {
			return ResultStatus::error;
		}

		return AfterInitialization();
	}


	ResultStatus RegularInitialization(uint8 rankLength) override {
		return ResultStatus::ok;
	}


	ResultStatus InjectedInitialization(uint8 rankLength) override {
		return ResultStatus::notSupported;
	}


	Result<uint32> SetRegularChannel(const RegularChannel &channel, uint8 rank) override {
		if (rank == 0 || rank > maxRegularChannels) {
			return ResultStatus::error;
		}

		adc_oneshot_chan_cfg_t config = {};
		config.bitwidth = CastBitwidth();
		config.atten = attenuation;

		if (adc_oneshot_config_channel(unitHandle, static_cast<adc_channel_t>(channel.channel), &config) != ESP_OK) {
			return ResultStatus::error;
		}

		uint8 idx = rank - 1;
		DeleteCalibration(regularChannels[idx]);
		regularChannels[idx].channel = static_cast<adc_channel_t>(channel.channel);
		regularChannels[idx].attenuation = attenuation;
		regularChannels[idx].configured = true;
		if (rank > regularCount) {
			regularCount = rank;
		}

		return 0;
	}


	Result<uint32> SetInjectedChannel(const InjecteChannel &channel, uint8 rank) override {
		return ResultStatus::notSupported;
	}


	ResultStatus ReadByteArray(uint8 *buffer, uint16 size) override {
		uint8 resBytes = GetResolutionByte();
		uint16 count = size / resBytes;

		for (uint16 i = 0; i < count && i < regularCount; i++) {
			if (!regularChannels[i].configured) {
				continue;
			}

			int raw = 0;
			if (adc_oneshot_read(unitHandle, regularChannels[i].channel, &raw) != ESP_OK) {
				return ResultStatus::error;
			}

			if (resBytes == 2) {
				reinterpret_cast<uint16*>(buffer)[i] = static_cast<uint16>(raw);
			} else {
				buffer[i] = static_cast<uint8>(raw);
			}
		}

		return ResultStatus::ok;
	}


	ResultStatus ReadByteArrayAsync(uint8 *buffer, uint16 size) override {
		return ResultStatus::notSupported;
	}


private:
	adc_bitwidth_t CastBitwidth() const {
		return static_cast<adc_bitwidth_t>(static_cast<uint8>(parameters.resolution));
	}


	bool IsRankConfigured(uint8 rank) const {
		return rank != 0 && rank <= regularCount && regularChannels[rank - 1].configured;
	}


	void DeleteCalibration(ChannelEntry &entry) {
		if (entry.calibration == nullptr) {
			return;
		}

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
		adc_cali_delete_scheme_curve_fitting(entry.calibration);
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
		adc_cali_delete_scheme_line_fitting(entry.calibration);
#endif

		entry.calibration = nullptr;
	}


	void DeleteCalibrations() {
		for (uint8 i = 0; i < maxRegularChannels; i++) {
			DeleteCalibration(regularChannels[i]);
		}
	}
};
