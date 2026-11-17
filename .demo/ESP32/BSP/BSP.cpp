#include "BSP.h"
#include <esp_timer.h>


AUART BSP::consoleSerial = { UART_NUM_0, 21, 20 }; // TX GPIO21, RX GPIO20
AIWDG BSP::mcuWatchDog;                            // ESP-IDF task watchdog (TWDT)
AGPIO BSP::ledPin = { 4, false };                  // External LED, active high


void BSP::InitSystemTick(uint32 ms) {
	// ESP-IDF owns the FreeRTOS tick, so a periodic esp_timer drives System::TickHandler().
	static esp_timer_handle_t tickTimer = nullptr;

	esp_timer_create_args_t timerArgs = {};
	timerArgs.callback = [](void* argument) {
		System::TickHandler();
	};
	timerArgs.dispatch_method = ESP_TIMER_TASK;
	timerArgs.name = "vhal_tick";

	ESP_ERROR_CHECK(esp_timer_create(&timerArgs, &tickTimer));
	ESP_ERROR_CHECK(esp_timer_start_periodic(tickTimer, ms * 1000));
}


void BSP::InitIO() {
	ledPin.Reset().SetParameters({ AGPIO::Mode::Output });
}
