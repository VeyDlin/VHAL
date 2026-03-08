#pragma once
#include <VHAL.h>


class BSP {
public:
	static AUART consoleSerial;
	static AIWDG mcuWatchDog;
	static AGPIO ledPin;


public:
	static void Init() {
		InitSystem();
		InitClock();
		InitSystemTick(1, 0);
		System::Init();

		InitAdapterPeripheryEvents();
		InitIO();
	}


private:
	static void InitSystem();
	static void InitClock();
	static void InitSystemTick(uint32 ms, uint32 tickPriority);
	static void InitAdapterPeripheryEvents();
	static void InitIO();
};
