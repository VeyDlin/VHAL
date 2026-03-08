#include <Application.h>
#include <AppThreads/MainTask.h>
#include <AppThreads/McuWatchDogTask.h>


MainTask Application::main;
McuWatchDogTask Application::mcuWatchDog;



void Application::Init() {
	InitSystemHandle();

	RTOS::CreateThread(main);

	if (useWatchDog) {
		RTOS::CreateThread(mcuWatchDog, ThreadPriority::realtime);
	}

	RTOS::Start();
}



void Application::InitSystemHandle() {
	// For use System::DelayMs() in RTOS
	System::rtosDelayMsHandle = [](auto delay) {
		if (RTOS::IsSchedulerRun()) {
			RTOS::Sleep(std::chrono::milliseconds(delay));
			return true;
		}
		return false;
	};

	// For log System::CriticalError()
	System::criticalErrorHandle = [](auto message, auto file, auto line) {
		// Save to FLASH or/and log
	};
}
