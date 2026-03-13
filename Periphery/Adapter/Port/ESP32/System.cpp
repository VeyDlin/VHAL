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


void System::InitPlatform() {
#ifdef VHAL_SYSTEM_CONSOLE
	// Console
	SetWriteHandler([](const char* str, size_t size) {
		fwrite(str, 1, size, stdout);
	});
	
	SetReadHandler([]() -> int {
		return fgetc(stdin);
	});
#endif

#if defined(VHAL_RTOS) && defined(VHAL_RTOS_FREERTOS)
	// For use System::DelayMs() in RTOS
	rtosDelayMsHandle = [](auto delay) {
		if (OS::RTOS::IsSchedulerRun()) {
			OS::RTOS::Sleep(std::chrono::milliseconds(delay));
			return true;
		}
		return false;
	};
#endif
}


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
