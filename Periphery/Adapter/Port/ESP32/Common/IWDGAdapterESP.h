#pragma once
#include <Adapter/IWDGAdapter.h>


using AIWDG = class IWDGAdapterESP;


// IWDG interface over the ESP-IDF task watchdog (TWDT). The deadline is the TWDT timeout. Start subscribes the
// calling task, so Reset must be called from that same task. A missed deadline triggers a panic and a reset.
class IWDGAdapterESP : public IWDGAdapter<void> {
public:
	IWDGAdapterESP() = default;


	void Start() override {
		ESP_ERROR_CHECK(esp_task_wdt_add(nullptr));
		Reset();
	}


	void Reset() override {
		ESP_ERROR_CHECK(esp_task_wdt_reset());
	}


	uint32 GetDeadlineMs() const override {
		return parameters.minDeadlineMs;
	}


protected:
	ResultStatus Initialization() override {
		auto status = BeforeInitialization();
		if (status != ResultStatus::ok) {
			return status;
		}

		esp_task_wdt_config_t config = {};
		config.timeout_ms = parameters.minDeadlineMs;
		config.idle_core_mask = (1U << portNUM_PROCESSORS) - 1U;
		config.trigger_panic = true;

		// ESP-IDF may already run the TWDT (CONFIG_ESP_TASK_WDT_INIT); otherwise it is created here.
		esp_err_t result = esp_task_wdt_reconfigure(&config);
		if (result == ESP_ERR_INVALID_STATE) {
			result = esp_task_wdt_init(&config);
		}
		if (result != ESP_OK) {
			return ResultStatus::error;
		}

		return AfterInitialization();
	}


	ResultStatus CalculateDividers() override {
		return ResultStatus::ok;
	}
};
