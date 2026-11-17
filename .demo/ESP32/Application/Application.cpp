#include <Application.h>
#include <AppThreads/MainTask.h>
#include <AppThreads/McuWatchDogTask.h>


MainTask Application::main;
McuWatchDogTask Application::mcuWatchDog;


void Application::Init() {
	InitSystemHandle();

	RTOS::CreateThread(main, ThreadPriority::normal, "main");

	if (useWatchDog) {
		RTOS::CreateThread(mcuWatchDog, ThreadPriority::realtime, "watchdog");
	}

	// ESP-IDF starts the scheduler before app_main(), so there is no RTOS::Start() here.
}


void Application::InitSystemHandle() {
	// System::DelayMs() already sleeps through the RTOS: the ESP32 port sets rtosDelayMsHandle in System::Init().

	// For log System::CriticalError()
	System::criticalErrorHandle = [](auto message, auto file, auto line) {
		// Save to FLASH or/and log
	};
}
