#pragma once
#include <BSP.h>

using namespace OS;


class Application {
public:
	static class MainTask main;
	static class McuWatchDogTask mcuWatchDog;


private:
	static constexpr bool useWatchDog = false;


public:
	static void Init();


private:
	static void InitSystemHandle();
};
