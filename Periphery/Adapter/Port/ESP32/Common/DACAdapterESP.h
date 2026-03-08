#pragma once
#include <Adapter/DACAdapter.h>


using ADAC = class DACAdapterESP;


class DACAdapterESP : public DACAdapter<void> {
protected:
	dac_oneshot_handle_t dacHandle = nullptr;
	dac_channel_t channel;


public:
	DACAdapterESP() = default;

	DACAdapterESP(dac_channel_t dacChannel)
		: channel(dacChannel) { }

	~DACAdapterESP() {
		if (dacHandle) {
			dac_oneshot_del_channel(dacHandle);
		}
	}


	void IrqHandler() override { }


	ResultStatus Write(uint16 val) override {
		if (!dacHandle) {
			return ResultStatus::error;
		}
		if (dac_oneshot_output_voltage(dacHandle, static_cast<uint8>(val & 0xFF)) != ESP_OK) {
			return ResultStatus::error;
		}
		return ResultStatus::ok;
	}


	ResultStatus Enable() override {
		if (dacHandle) {
			return ResultStatus::ok;
		}
		return Initialization();
	}


	ResultStatus Disable() override {
		if (dacHandle) {
			dac_oneshot_del_channel(dacHandle);
			dacHandle = nullptr;
		}
		return ResultStatus::ok;
	}


	void EnableDMARequest() override { }
	void DisableDMARequest() override { }
	void EnableTrigger() override { }
	void DisableTrigger() override { }


	ResultStatus WriteContinuous(uint16 *buffer, uint32 count) override {
		return ResultStatus::notSupported;
	}

	ResultStatus StopDMA() override {
		return ResultStatus::notSupported;
	}


protected:
	ResultStatus Initialization() override {
		auto status = BeforeInitialization();
		if (status != ResultStatus::ok) {
			return status;
		}

		if (dacHandle) {
			dac_oneshot_del_channel(dacHandle);
			dacHandle = nullptr;
		}

		dac_oneshot_config_t config = {};
		config.chan_id = channel;

		if (dac_oneshot_new_channel(&config, &dacHandle) != ESP_OK) {
			return ResultStatus::error;
		}

		return AfterInitialization();
	}


	uint32 GetDataRegisterAddress() override {
		return 0;
	}
};
