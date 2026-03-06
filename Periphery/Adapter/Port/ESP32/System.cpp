#include <VHAL.h>

#if defined(VHAL_ESP32)

#include "esp_timer.h"
#include "esp_system.h"
#include "esp_cpu.h"
#include "esp_mac.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"


uint32 SystemCoreClock = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ * 1000000;

static portMUX_TYPE s_criticalMux = portMUX_INITIALIZER_UNLOCKED;


uint32 System::GetCoreTick() {
	return esp_cpu_get_cycle_count();
}


bool System::InitDelayUs() {
	return true;
}


void System::DelayUs(uint32 delay) {
	esp_rom_delay_us(delay);
}


void System::Reset() {
	esp_restart();
}


void System::CriticalSection(bool isEnable) {
	if (isEnable) {
		portENTER_CRITICAL(&s_criticalMux);
	} else {
		portEXIT_CRITICAL(&s_criticalMux);
	}
}


bool System::IsInterrupt() {
	return xPortInIsrContext();
}


System::DeviceId System::GetDeviceId() {
	DeviceId id = {};
	esp_efuse_mac_get_default(id.bytes);
	return id;
}

#endif
