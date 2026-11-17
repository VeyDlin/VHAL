#pragma once
#include <VHAL.h>


class BSP {
public:
	static AUART consoleSerial;
	static AIWDG mcuWatchDog;
	static AGPIO ledPin;


public:
	// ESP-IDF sets up the clock and starts FreeRTOS before app_main(), so there is no InitSystem/InitClock here.
	static void Init() {
		InitSystemTick(1);
		System::Init();

		InitIO();
	}


private:
	static void InitSystemTick(uint32 ms);
	static void InitIO();
};
