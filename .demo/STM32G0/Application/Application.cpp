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



// With configSUPPORT_STATIC_ALLOCATION == 1 the kernel takes the idle and timer task memory from the application.
extern "C" void vApplicationGetIdleTaskMemory(StaticTask_t **ppxIdleTaskTCBBuffer, StackType_t **ppxIdleTaskStackBuffer, uint32_t *pulIdleTaskStackSize) {
	static StaticTask_t idleTaskContext;
	static StackType_t idleTaskStack[configMINIMAL_STACK_SIZE];

	*ppxIdleTaskTCBBuffer = &idleTaskContext;
	*ppxIdleTaskStackBuffer = idleTaskStack;
	*pulIdleTaskStackSize = configMINIMAL_STACK_SIZE;
}


extern "C" void vApplicationGetTimerTaskMemory(StaticTask_t **ppxTimerTaskTCBBuffer, StackType_t **ppxTimerTaskStackBuffer, uint32_t *pulTimerTaskStackSize) {
	static StaticTask_t timerTaskContext;
	static StackType_t timerTaskStack[configTIMER_TASK_STACK_DEPTH];

	*ppxTimerTaskTCBBuffer = &timerTaskContext;
	*ppxTimerTaskStackBuffer = timerTaskStack;
	*pulTimerTaskStackSize = configTIMER_TASK_STACK_DEPTH;
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
