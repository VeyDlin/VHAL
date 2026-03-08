#pragma once
#include <Adapter/Port/ESP32/Adapter/MCPWMAdapter.h>


using AMCPWM = class MCPWMAdapterESP;


class MCPWMAdapterESP : public MCPWMAdapter {
public:
	struct CountMode {
		static inline constexpr CountModeOption Up     { MCPWM_TIMER_COUNT_MODE_UP };
		static inline constexpr CountModeOption Down   { MCPWM_TIMER_COUNT_MODE_DOWN };
		static inline constexpr CountModeOption UpDown { MCPWM_TIMER_COUNT_MODE_UP_DOWN };
	};


public:
	MCPWMAdapterESP() = default;
	MCPWMAdapterESP(int groupId) : MCPWMAdapter(groupId) { }

	~MCPWMAdapterESP() {
		Deinit();
	}


	ResultStatus SetParameters(Parameters val) override {
		parameters = val;
		return Initialization();
	}


	ResultStatus AddComparator(uint32 compareValue = 0) override {
		if (comparatorCount >= maxComparators || !operHandle) {
			return ResultStatus::error;
		}

		mcpwm_comparator_config_t config = {};
		config.flags.update_cmp_on_tez = true;

		if (mcpwm_new_comparator(operHandle, &config, &comparators[comparatorCount]) != ESP_OK) {
			return ResultStatus::error;
		}

		if (mcpwm_comparator_set_compare_value(comparators[comparatorCount], compareValue) != ESP_OK) {
			return ResultStatus::error;
		}

		comparatorCount++;
		return ResultStatus::ok;
	}


	ResultStatus AddGenerator(GeneratorConfig config, uint8 comparatorIndex = 0) override {
		if (generatorCount >= maxGenerators || !operHandle) {
			return ResultStatus::error;
		}
		if (comparatorIndex >= comparatorCount) {
			return ResultStatus::error;
		}

		mcpwm_generator_config_t gen_config = {};
		gen_config.gen_gpio_num = config.pin;

		if (mcpwm_new_generator(operHandle, &gen_config, &generators[generatorCount]) != ESP_OK) {
			return ResultStatus::error;
		}

		if (mcpwm_generator_set_action_on_timer_event(generators[generatorCount],
				MCPWM_GEN_TIMER_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, MCPWM_TIMER_EVENT_EMPTY, MCPWM_GEN_ACTION_HIGH)) != ESP_OK) {
			return ResultStatus::error;
		}

		if (mcpwm_generator_set_action_on_compare_event(generators[generatorCount],
				MCPWM_GEN_COMPARE_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, comparators[comparatorIndex], MCPWM_GEN_ACTION_LOW)) != ESP_OK) {
			return ResultStatus::error;
		}

		generatorCount++;
		return ResultStatus::ok;
	}


	ResultStatus SetCompare(uint8 comparatorIndex, uint32 value) override {
		if (comparatorIndex >= comparatorCount) {
			return ResultStatus::error;
		}
		return (mcpwm_comparator_set_compare_value(comparators[comparatorIndex], value) == ESP_OK)
			? ResultStatus::ok : ResultStatus::error;
	}


	ResultStatus SetDeadTime(uint8 genA, uint8 genB, DeadTimeConfig config) override {
		if (genA >= generatorCount || genB >= generatorCount) {
			return ResultStatus::error;
		}

		mcpwm_dead_time_config_t dt_config = {};
		dt_config.posedge_delay_ticks = config.posedgeDelayTicks;
		dt_config.negedge_delay_ticks = 0;

		if (mcpwm_generator_set_dead_time(generators[genA], generators[genA], &dt_config) != ESP_OK) {
			return ResultStatus::error;
		}

		dt_config.posedge_delay_ticks = 0;
		dt_config.negedge_delay_ticks = config.negedgeDelayTicks;

		if (mcpwm_generator_set_dead_time(generators[genA], generators[genB], &dt_config) != ESP_OK) {
			return ResultStatus::error;
		}

		return ResultStatus::ok;
	}


	ResultStatus Start() override {
		if (!timerHandle) {
			return ResultStatus::error;
		}
		if (mcpwm_timer_enable(timerHandle) != ESP_OK) {
			return ResultStatus::error;
		}
		return (mcpwm_timer_start_stop(timerHandle, MCPWM_TIMER_START_NO_STOP) == ESP_OK)
			? ResultStatus::ok : ResultStatus::error;
	}


	ResultStatus Stop() override {
		if (!timerHandle) {
			return ResultStatus::error;
		}
		return (mcpwm_timer_start_stop(timerHandle, MCPWM_TIMER_STOP_FULL) == ESP_OK)
			? ResultStatus::ok : ResultStatus::error;
	}


protected:
	static constexpr uint8 maxGenerators = 2;
	static constexpr uint8 maxComparators = 2;

	mcpwm_timer_handle_t timerHandle = nullptr;
	mcpwm_oper_handle_t operHandle = nullptr;
	mcpwm_cmpr_handle_t comparators[maxComparators] = {};
	mcpwm_gen_handle_t generators[maxGenerators] = {};
	uint8 comparatorCount = 0;
	uint8 generatorCount = 0;

	ResultStatus Initialization() override {
		auto status = BeforeInitialization();
		if (status != ResultStatus::ok) {
			return status;
		}

		Deinit();

		mcpwm_timer_config_t timer_cfg = {};
		timer_cfg.group_id = groupId;
		timer_cfg.clk_src = MCPWM_TIMER_CLK_SRC_DEFAULT;
		timer_cfg.resolution_hz = parameters.resolutionHz;
		timer_cfg.period_ticks = parameters.periodTicks;
		timer_cfg.count_mode = static_cast<mcpwm_timer_count_mode_t>(parameters.countMode.Get());

		if (mcpwm_new_timer(&timer_cfg, &timerHandle) != ESP_OK) {
			return ResultStatus::error;
		}

		mcpwm_operator_config_t oper_cfg = {};
		oper_cfg.group_id = groupId;

		if (mcpwm_new_operator(&oper_cfg, &operHandle) != ESP_OK) {
			return ResultStatus::error;
		}

		if (mcpwm_operator_connect_timer(operHandle, timerHandle) != ESP_OK) {
			return ResultStatus::error;
		}

		return AfterInitialization();
	}


private:
	void Deinit() {
		for (uint8 i = 0; i < generatorCount; i++) {
			if (generators[i]) {
				mcpwm_del_generator(generators[i]);
				generators[i] = nullptr;
			}
		}
		generatorCount = 0;

		for (uint8 i = 0; i < comparatorCount; i++) {
			if (comparators[i]) {
				mcpwm_del_comparator(comparators[i]);
				comparators[i] = nullptr;
			}
		}
		comparatorCount = 0;

		if (operHandle) {
			mcpwm_del_operator(operHandle);
			operHandle = nullptr;
		}

		if (timerHandle) {
			mcpwm_timer_disable(timerHandle);
			mcpwm_del_timer(timerHandle);
			timerHandle = nullptr;
		}
	}
};
