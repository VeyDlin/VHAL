#pragma once

#include "driver/gpio.h"

#ifdef VHAL_ESP32_UART
	#include "driver/uart.h"
#endif

#ifdef VHAL_ESP32_SPI
	#include "driver/spi_master.h"
#endif

#ifdef VHAL_ESP32_I2C
	#include "driver/i2c_master.h"
#endif

#ifdef VHAL_ESP32_GPTIMER
	#include "driver/gptimer.h"
#endif

#ifdef VHAL_ESP32_LEDC
	#include "driver/ledc.h"
#endif

#ifdef VHAL_ESP32_MCPWM
	#include "driver/mcpwm_prelude.h"
#endif

#ifdef VHAL_ESP32_I2S
	#include "driver/i2s_std.h"
	#include "driver/i2s_tdm.h"
#endif

#ifdef VHAL_ESP32_DSI
	#include "esp_lcd_mipi_dsi.h"
	#include "esp_lcd_panel_ops.h"
	#include "esp_lcd_panel_io.h"
#endif

#ifdef VHAL_ESP32_ADC
	#include "esp_adc/adc_oneshot.h"
#endif

#ifdef VHAL_ESP32_PPA
	#include "driver/ppa.h"
#endif

#ifdef VHAL_ESP32_LDO
	#include "esp_ldo_regulator.h"
#endif

#ifdef VHAL_ESP32_DAC
	#include "soc/soc_caps.h"
	#if !SOC_DAC_SUPPORTED
		#error "VHAL: DAC is not supported on this ESP32 chip"
	#endif
	#include "driver/dac_oneshot.h"
#endif
