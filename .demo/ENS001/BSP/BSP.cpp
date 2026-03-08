#include "BSP.h"


AUART BSP::uart0(CMSDK_UART0, SystemCoreClock);
ADCAdapter001 BSP::adc(CMSDK_ADC);
WaveGeneratorAdapter001 BSP::waveGen(WAVE_GEN_DRVA_BLK0);
BoostAdapter001 BSP::boost(CMSDK_ANAC);
ATIM BSP::timer0(CMSDK_TIMER0, SystemCoreClock);



void BSP::InitClock() {
	// HSI 32 MHz (already default after SystemInit)
}



void BSP::InitSystemTick() {
	SysTick_Config(SystemCoreClock / 1000);
}



void BSP::InitPeripheralClocks() {
	// Enable peripheral clocks on APB bus
	// bit 0:  UART0
	// bit 12: WAVE_GEN
	// bit 13: ADC
	// bit 14: ANALOG (for Boost)
	CMSDK_SYSCON->APB_CLKEN |= (1 << 0)
							 | (1 << 8)
							 | (1 << 12)
							 | (1 << 13)
							 | (1 << 14);
}



extern "C" void SystemInit() {
	// HSI 32 MHz
	CMSDK_SYSCON->HSI_CTRL |= (3 << 4);

	// Select HSI as system clock source
	CMSDK_SYSCON->CLK_CFG = 0;
	while ((CMSDK_SYSCON->CLK_CFG >> 2) & 0x1) {}

	// AHB div1, APB div1 (bits 8-10 = AHB, bits 12-14 = APB, all zero = div1)

	// Reset all peripheral clocks
	CMSDK_SYSCON->APB_CLKEN = 0;

	SystemCoreClock = 32000000;
}
