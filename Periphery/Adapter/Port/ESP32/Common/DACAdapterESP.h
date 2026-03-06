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


	Status::statusType Write(uint16 val) override {
		if (!dacHandle) {
			return Status::error;
		}
		if (dac_oneshot_output_voltage(dacHandle, static_cast<uint8>(val & 0xFF)) != ESP_OK) {
			return Status::error;
		}
		return Status::ok;
	}


	Status::statusType Enable() override {
		if (dacHandle) {
			return Status::ok;
		}
		return Initialization();
	}


	Status::statusType Disable() override {
		if (dacHandle) {
			dac_oneshot_del_channel(dacHandle);
			dacHandle = nullptr;
		}
		return Status::ok;
	}


	void EnableDMARequest() override { }
	void DisableDMARequest() override { }
	void EnableTrigger() override { }
	void DisableTrigger() override { }


	Status::statusType WriteContinuous(uint16 *buffer, uint32 count) override {
		return Status::notSupported;
	}

	Status::statusType StopDMA() override {
		return Status::notSupported;
	}


protected:
	Status::statusType Initialization() override {
		auto status = BeforeInitialization();
		if (status != Status::ok) {
			return status;
		}

		if (dacHandle) {
			dac_oneshot_del_channel(dacHandle);
			dacHandle = nullptr;
		}

		dac_oneshot_config_t config = {};
		config.chan_id = channel;

		if (dac_oneshot_new_channel(&config, &dacHandle) != ESP_OK) {
			return Status::error;
		}

		return AfterInitialization();
	}


	uint32 GetDataRegisterAddress() override {
		return 0;
	}
};
