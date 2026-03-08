#pragma once
#include <VHAL.h>


// Pin mapping
namespace Pin {
	constexpr uint8 Led = 6;

	constexpr uint8 Uart0Rx = 2;
	constexpr uint8 Uart0Tx = 3;
	constexpr uint8 Uart0Alt = 1;

	constexpr uint8 Adc1 = 21;
	constexpr uint8 Adc2 = 22;
	constexpr uint8 Adc3 = 23;
}


class BSP {
public:
	static AUART uart0;
	static ADCAdapter001 adc;
	static WaveGeneratorAdapter001 waveGen;
	static BoostAdapter001 boost;
	static ATIM timer0;

public:
	static void Init() {
		InitClock();
		InitSystemTick();
		System::Init();
		InitPeripheralClocks();
	}

private:
	static void InitClock();
	static void InitSystemTick();
	static void InitPeripheralClocks();
};
