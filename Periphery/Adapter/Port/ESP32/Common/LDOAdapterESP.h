#pragma once
#include <Adapter/Port/ESP32/Adapter/LDOAdapter.h>


using ALDO = class LDOAdapterESP;


class LDOAdapterESP : public LDOAdapter {
public:
	struct Flag {
		static inline constexpr FlagOption Adjustable  { 1 << 0 };
		static inline constexpr FlagOption OwnedByHw   { 1 << 1 };
	};


	LDOAdapterESP() = default;

	~LDOAdapterESP() {
		if (handle) {
			esp_ldo_release_channel(handle);
		}
	}


	ResultStatus SetParameters(Parameters val) override {
		parameters = val;
		return Initialization();
	}


	ResultStatus Acquire() override {
		if (handle) {
			return ResultStatus::ok;
		}

		esp_ldo_channel_config_t cfg = {};
		cfg.chan_id = parameters.channelId;
		cfg.voltage_mv = parameters.voltageMv;
		cfg.flags.adjustable = parameters.flags.Has(Flag::Adjustable);
		cfg.flags.owned_by_hw = parameters.flags.Has(Flag::OwnedByHw);

		if (esp_ldo_acquire_channel(&cfg, &handle) != ESP_OK) {
			return ResultStatus::error;
		}

		return ResultStatus::ok;
	}


	ResultStatus Release() override {
		if (!handle) {
			return ResultStatus::ok;
		}

		if (esp_ldo_release_channel(handle) != ESP_OK) {
			return ResultStatus::error;
		}

		handle = nullptr;
		return ResultStatus::ok;
	}


	ResultStatus SetVoltage(uint32 voltageMv) override {
		if (!handle) {
			return ResultStatus::error;
		}

		parameters.voltageMv = voltageMv;

		if (esp_ldo_channel_adjust_voltage(handle, voltageMv) != ESP_OK) {
			return ResultStatus::error;
		}

		return ResultStatus::ok;
	}


protected:
	esp_ldo_channel_handle_t handle = nullptr;

	ResultStatus Initialization() override {
		auto status = BeforeInitialization();
		if (status != ResultStatus::ok) {
			return status;
		}

		if (handle) {
			esp_ldo_release_channel(handle);
			handle = nullptr;
		}

		status = Acquire();
		if (status != ResultStatus::ok) {
			return status;
		}

		return AfterInitialization();
	}
};
