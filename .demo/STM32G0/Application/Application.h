#pragma once
#include <BSP.h>

using namespace OS;


class Application {
private:
	static constexpr bool useWatchDog = false;


public:
	static class MainTask main;
	static class McuWatchDogTask mcuWatchDog;


public:
	static void Init();


private:
	static void InitSystemHandle();
};
