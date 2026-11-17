#pragma once

#include "soc/soc_caps.h"
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

#ifdef VHAL_ESP32_IWDG
	#include "esp_task_wdt.h"
#endif

#ifdef VHAL_ESP32_LEDC
	#include "driver/ledc.h"
#endif

#ifdef VHAL_ESP32_MCPWM
	#include "driver/mcpwm_prelude.h"
#endif

#ifdef VHAL_ESP32_I2S
	#include "driver/i2s_std.h"
	#include "driver/i2s_pdm.h"
	#include "driver/i2s_tdm.h"
#endif

#ifdef VHAL_ESP32_DSI
	#include "esp_lcd_mipi_dsi.h"
	#include "esp_lcd_panel_ops.h"
	#include "esp_lcd_panel_io.h"
#endif

#ifdef VHAL_ESP32_ADC
	#include "esp_adc/adc_oneshot.h"
	#include "esp_adc/adc_cali.h"
	#include "esp_adc/adc_cali_scheme.h"
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

#ifdef VHAL_ESP32_WIFI
	#if !SOC_WIFI_SUPPORTED
		#error "VHAL: Wi-Fi is not supported on this ESP32 chip"
	#endif
	#include "esp_wifi.h"
	#include "esp_netif.h"
	#include "esp_event.h"
	#include "nvs_flash.h"
#endif

#ifdef VHAL_ESP32_BLE
	#include "sdkconfig.h"
	#if !CONFIG_BT_NIMBLE_ENABLED
		#error "VHAL: BLE needs NimBLE: set CONFIG_BT_ENABLED and CONFIG_BT_NIMBLE_ENABLED in sdkconfig"
	#endif
	#if !CONFIG_BT_NIMBLE_GAP_SERVICE
		#error "VHAL: BLE needs the NimBLE GAP service: set CONFIG_BT_NIMBLE_GAP_SERVICE in sdkconfig"
	#endif
	#include "nimble/nimble_port.h"
	#include "nimble/nimble_port_freertos.h"
	#include "host/ble_hs.h"
	#include "host/ble_att.h"
	#include "host/ble_gap.h"
	#include "host/ble_gatt.h"
	#include "host/ble_hs_adv.h"
	#include "host/ble_hs_id.h"
	#include "host/ble_hs_mbuf.h"
	#include "host/ble_sm.h"
	#include "host/ble_store.h"
	#include "os/os_mbuf.h"
	#include "services/gap/ble_svc_gap.h"
	#include "services/gatt/ble_svc_gatt.h"
	#include "esp_bt.h"
	#include "esp_random.h"
	#include "nvs_flash.h"
#endif

#ifdef VHAL_ESP32_SOCKET
	#include "sdkconfig.h"
	#include "lwip/sockets.h"
	#include "lwip/netdb.h"
	#include "esp_tls.h"
	#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
		#include "esp_crt_bundle.h"
	#endif
#endif
