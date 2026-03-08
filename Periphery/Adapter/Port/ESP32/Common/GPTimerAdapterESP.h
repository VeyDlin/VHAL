#pragma once
#include <Adapter/Port/ESP32/Adapter/GPTimerAdapter.h>


using AGPTimer = class GPTimerAdapterESP;


class GPTimerAdapterESP : public GPTimerAdapter {
public:
	GPTimerAdapterESP() = default;

	~GPTimerAdapterESP() {
		if (timerHandle) {
			gptimer_stop(timerHandle);
			gptimer_disable(timerHandle);
			gptimer_del_timer(timerHandle);
		}
	}


	ResultStatus SetParameters(Parameters val) override {
		parameters = val;
		return Initialization();
	}


	ResultStatus Start() override {
		if (!timerHandle) {
			return ResultStatus::error;
		}
		return (gptimer_start(timerHandle) == ESP_OK) ? ResultStatus::ok : ResultStatus::error;
	}


	ResultStatus Stop() override {
		if (!timerHandle) {
			return ResultStatus::error;
		}
		return (gptimer_stop(timerHandle) == ESP_OK) ? ResultStatus::ok : ResultStatus::error;
	}


	ResultStatus SetAlarm(uint64 alarm, bool reload) override {
		if (!timerHandle) {
			return ResultStatus::error;
		}
		parameters.alarmCount = alarm;
		parameters.autoReload = reload;

		gptimer_alarm_config_t alarm_config = {};
		alarm_config.alarm_count = alarm;
		alarm_config.reload_count = 0;
		alarm_config.flags.auto_reload_on_alarm = reload;

		return (gptimer_set_alarm_action(timerHandle, &alarm_config) == ESP_OK) ? ResultStatus::ok : ResultStatus::error;
	}


	ResultStatus SetCount(uint64 count) override {
		if (!timerHandle) {
			return ResultStatus::error;
		}
		return (gptimer_set_raw_count(timerHandle, count) == ESP_OK) ? ResultStatus::ok : ResultStatus::error;
	}


	uint64 GetCount() override {
		uint64_t count = 0;
		if (timerHandle) {
			gptimer_get_raw_count(timerHandle, &count);
		}
		return count;
	}


protected:
	gptimer_handle_t timerHandle = nullptr;

	ResultStatus Initialization() override {
		auto status = BeforeInitialization();
		if (status != ResultStatus::ok) {
			return status;
		}

		if (timerHandle) {
			gptimer_stop(timerHandle);
			gptimer_disable(timerHandle);
			gptimer_del_timer(timerHandle);
			timerHandle = nullptr;
		}

		gptimer_config_t config = {};
		config.clk_src = GPTIMER_CLK_SRC_DEFAULT;
		config.direction = CastDirection();
		config.resolution_hz = parameters.resolutionHz;

		if (gptimer_new_timer(&config, &timerHandle) != ESP_OK) {
			return ResultStatus::error;
		}

		gptimer_event_callbacks_t cbs = {};
		cbs.on_alarm = AlarmCallback;

		if (gptimer_register_event_callbacks(timerHandle, &cbs, this) != ESP_OK) {
			return ResultStatus::error;
		}

		if (gptimer_enable(timerHandle) != ESP_OK) {
			return ResultStatus::error;
		}

		if (parameters.alarmCount > 0) {
			status = SetAlarm(parameters.alarmCount, parameters.autoReload);
			if (status != ResultStatus::ok) {
				return status;
			}
		}

		return AfterInitialization();
	}


private:
	gptimer_count_direction_t CastDirection() const {
		switch (parameters.direction) {
			case CountDirection::Up:   return GPTIMER_COUNT_UP;
			case CountDirection::Down: return GPTIMER_COUNT_DOWN;
		}
		return GPTIMER_COUNT_UP;
	}

	static bool IRAM_ATTR AlarmCallback(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *user_ctx) {
		auto *self = static_cast<GPTimerAdapterESP*>(user_ctx);
		if (self->onAlarm) {
			self->onAlarm();
		}
		return false;
	}
};
