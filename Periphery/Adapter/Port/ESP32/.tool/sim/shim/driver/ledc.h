#pragma once
// Desktop stand-in for the ESP-IDF LEDC driver API that LEDCAdapterESP.h calls. Only the callback registry is modelled: each
// ledc_cb_register() stores the fade callback and its argument per speed mode and channel, the way the driver keeps them, so a
// test can read what the driver would call at a fade end. Every other call succeeds and does nothing.
#include <cstdint>


typedef int esp_err_t;

#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_STATE 0x103


enum ledc_mode_t {
	LEDC_LOW_SPEED_MODE = 0,
	LEDC_HIGH_SPEED_MODE = 1,
};


enum ledc_timer_t {
	LEDC_TIMER_0 = 0,
	LEDC_TIMER_1,
	LEDC_TIMER_2,
	LEDC_TIMER_3,
};


enum ledc_channel_t {
	LEDC_CHANNEL_0 = 0,
	LEDC_CHANNEL_1,
	LEDC_CHANNEL_2,
	LEDC_CHANNEL_3,
	LEDC_CHANNEL_4,
	LEDC_CHANNEL_5,
	LEDC_CHANNEL_6,
	LEDC_CHANNEL_7,
};


enum ledc_timer_bit_t {
	LEDC_TIMER_1_BIT = 1,
	LEDC_TIMER_2_BIT,
	LEDC_TIMER_3_BIT,
	LEDC_TIMER_4_BIT,
	LEDC_TIMER_5_BIT,
	LEDC_TIMER_6_BIT,
	LEDC_TIMER_7_BIT,
	LEDC_TIMER_8_BIT,
	LEDC_TIMER_9_BIT,
	LEDC_TIMER_10_BIT,
	LEDC_TIMER_11_BIT,
	LEDC_TIMER_12_BIT,
	LEDC_TIMER_13_BIT,
	LEDC_TIMER_14_BIT,
	LEDC_TIMER_15_BIT,
	LEDC_TIMER_16_BIT,
	LEDC_TIMER_17_BIT,
	LEDC_TIMER_18_BIT,
	LEDC_TIMER_19_BIT,
	LEDC_TIMER_20_BIT,
};


enum ledc_intr_type_t {
	LEDC_INTR_DISABLE = 0,
};


enum ledc_clk_cfg_t {
	LEDC_AUTO_CLK = 0,
};


enum ledc_fade_mode_t {
	LEDC_FADE_NO_WAIT = 0,
};


enum ledc_cb_event_t {
	LEDC_FADE_END_EVT = 0,
};


struct ledc_channel_config_t {
	ledc_mode_t speed_mode;
	ledc_channel_t channel;
	ledc_intr_type_t intr_type;
	ledc_timer_t timer_sel;
	int gpio_num;
	uint32_t duty;
	uint32_t hpoint;
	struct {
		unsigned int output_invert : 1;
	} flags;
};


struct ledc_timer_config_t {
	ledc_mode_t speed_mode;
	ledc_timer_t timer_num;
	ledc_timer_bit_t duty_resolution;
	uint32_t freq_hz;
	ledc_clk_cfg_t clk_cfg;
};


struct ledc_cb_param_t {
	ledc_cb_event_t event;
	ledc_mode_t speed_mode;
	ledc_channel_t channel;
	uint32_t duty;
};


typedef bool (*ledc_cb_t)(const ledc_cb_param_t *param, void *user_arg);


struct ledc_cbs_t {
	ledc_cb_t fade_cb;
};


// The callback registry: what the driver would call, and with which argument, for each speed mode and channel. registerCount
// counts the ledc_cb_register() calls. The real driver allocates a channel's fade record on the first register or fade call
// for that channel, so a test uses the count to check when a channel is first registered.
struct LedcStubRegistry {
	ledc_cb_t fadeCallback[2][8];
	void *argument[2][8];
	int registerCount[2][8];
};

inline LedcStubRegistry ledcStubRegistry;


inline esp_err_t ledc_cb_register(ledc_mode_t speedMode, ledc_channel_t channel, ledc_cbs_t *callbacks, void *userArg) {
	ledcStubRegistry.fadeCallback[speedMode][channel] = callbacks->fade_cb;
	ledcStubRegistry.argument[speedMode][channel] = userArg;
	ledcStubRegistry.registerCount[speedMode][channel]++;
	return ESP_OK;
}


inline esp_err_t ledc_channel_config(const ledc_channel_config_t *) {
	return ESP_OK;
}


inline esp_err_t ledc_timer_config(const ledc_timer_config_t *) {
	return ESP_OK;
}


inline esp_err_t ledc_stop(ledc_mode_t, ledc_channel_t, uint32_t) {
	return ESP_OK;
}


inline esp_err_t ledc_set_duty_and_update(ledc_mode_t, ledc_channel_t, uint32_t, uint32_t) {
	return ESP_OK;
}


inline esp_err_t ledc_set_duty_with_hpoint(ledc_mode_t, ledc_channel_t, uint32_t, uint32_t) {
	return ESP_OK;
}


inline esp_err_t ledc_update_duty(ledc_mode_t, ledc_channel_t) {
	return ESP_OK;
}


inline esp_err_t ledc_set_freq(ledc_mode_t, ledc_timer_t, uint32_t) {
	return ESP_OK;
}


inline uint32_t ledc_get_freq(ledc_mode_t, ledc_timer_t) {
	return 0;
}


inline uint32_t ledc_get_duty(ledc_mode_t, ledc_channel_t) {
	return 0;
}


inline esp_err_t ledc_set_fade_with_time(ledc_mode_t, ledc_channel_t, uint32_t, int) {
	return ESP_OK;
}


inline esp_err_t ledc_fade_start(ledc_mode_t, ledc_channel_t, ledc_fade_mode_t) {
	return ESP_OK;
}


inline esp_err_t ledc_fade_func_install(int) {
	return ESP_OK;
}
