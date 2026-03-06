#pragma once
#include <Adapter/ADCAdapter.h>


using AADC = class ADCAdapterESP;


class ADCAdapterESP : public ADCAdapter<void> {
	static constexpr uint8 maxRegularChannels = 8;

protected:
	adc_oneshot_unit_handle_t unitHandle = nullptr;
	adc_unit_t unit;

	struct ChannelEntry {
		adc_channel_t channel;
		bool configured = false;
	};
	ChannelEntry regularChannels[maxRegularChannels] = {};
	uint8 regularCount = 0;


public:
	ADCAdapterESP() = default;

	ADCAdapterESP(adc_unit_t adcUnit)
		: unit(adcUnit) { }

	~ADCAdapterESP() {
		if (unitHandle) {
			adc_oneshot_del_unit(unitHandle);
		}
	}


	void IrqHandler() override { }

	void AbortRegular() override { state = Status::ready; }
	void AbortInjected() override { }
	void AbortWatchDog() override { }
	void AbortSampling() override { }
	void AbortConfigurationReady() override { }

	Status::statusType Calibration() override {
		return Status::notSupported;
	}


protected:
	Status::statusType Initialization() override {
		auto status = BeforeInitialization();
		if (status != Status::ok) {
			return status;
		}

		if (unitHandle) {
			adc_oneshot_del_unit(unitHandle);
			unitHandle = nullptr;
		}

		adc_oneshot_unit_init_cfg_t init_config = {};
		init_config.unit_id = unit;
		init_config.ulp_mode = ADC_ULP_MODE_DISABLE;

		if (adc_oneshot_new_unit(&init_config, &unitHandle) != ESP_OK) {
			return Status::error;
		}

		return AfterInitialization();
	}


	Status::statusType RegularInitialization(uint8 rankLength) override {
		return Status::ok;
	}


	Status::statusType InjectedInitialization(uint8 rankLength) override {
		return Status::notSupported;
	}


	Status::info<float> SetRegularChannel(const RegularChannel &channel, uint8 rank) override {
		auto result = Status::info<float>();

		if (rank == 0 || rank > maxRegularChannels) {
			result.type = Status::error;
			return result;
		}

		adc_oneshot_chan_cfg_t config = {};
		config.bitwidth = CastBitwidth();
		config.atten = ADC_ATTEN_DB_12;

		if (adc_oneshot_config_channel(unitHandle, static_cast<adc_channel_t>(channel.channel), &config) != ESP_OK) {
			result.type = Status::error;
			return result;
		}

		uint8 idx = rank - 1;
		regularChannels[idx].channel = static_cast<adc_channel_t>(channel.channel);
		regularChannels[idx].configured = true;
		if (rank > regularCount) {
			regularCount = rank;
		}

		result.type = Status::ok;
		result.data = 0;
		return result;
	}


	Status::info<float> SetInjectedChannel(const InjecteChannel &channel, uint8 rank) override {
		auto result = Status::info<float>();
		result.type = Status::notSupported;
		return result;
	}


	Status::statusType ReadByteArray(uint8 *buffer, uint16 size) override {
		uint8 resBytes = GetResolutionByte();
		uint16 count = size / resBytes;

		for (uint16 i = 0; i < count && i < regularCount; i++) {
			if (!regularChannels[i].configured) {
				continue;
			}

			int raw = 0;
			if (adc_oneshot_read(unitHandle, regularChannels[i].channel, &raw) != ESP_OK) {
				return Status::error;
			}

			if (resBytes == 2) {
				reinterpret_cast<uint16*>(buffer)[i] = static_cast<uint16>(raw);
			} else {
				buffer[i] = static_cast<uint8>(raw);
			}
		}

		return Status::ok;
	}


	Status::statusType ReadByteArrayAsync(uint8 *buffer, uint16 size) override {
		return Status::notSupported;
	}


private:
	adc_bitwidth_t CastBitwidth() const {
		switch (parameters.resolution) {
			case Resolution::B8:  return ADC_BITWIDTH_DEFAULT;
			case Resolution::B10: return ADC_BITWIDTH_10;
			case Resolution::B12: return ADC_BITWIDTH_12;
			default:              return ADC_BITWIDTH_DEFAULT;
		}
	}
};
