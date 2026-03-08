#include "BSP.h"


AUART BSP::consoleSerial = { USART1 };
AIWDG BSP::mcuWatchDog  = { IWDG, 32000 };
AGPIO BSP::ledPin        = { GPIOC, 6, true }; // inversion logic = true (OpenDrain)



void BSP::InitSystem() {
	LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_SYSCFG);
	LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_PWR);

	NVIC_SetPriority(PendSV_IRQn, 3);
	NVIC_SetPriority(SysTick_IRQn, 3);

	LL_SYSCFG_DisableDBATT(LL_SYSCFG_UCPD1_STROBE | LL_SYSCFG_UCPD2_STROBE);
}



void BSP::InitClock() {
	LL_FLASH_SetLatency(LL_FLASH_LATENCY_2);
	while (LL_FLASH_GetLatency() != LL_FLASH_LATENCY_2) {}

	LL_RCC_HSI_Enable();
	while (LL_RCC_HSI_IsReady() != 1) {}

	LL_RCC_LSI_Enable();
	while (LL_RCC_LSI_IsReady() != 1) {}

	LL_RCC_PLL_ConfigDomain_SYS(LL_RCC_PLLSOURCE_HSI, LL_RCC_PLLM_DIV_1, 8, LL_RCC_PLLR_DIV_2);
	LL_RCC_PLL_Enable();
	LL_RCC_PLL_EnableDomain_SYS();
	while (LL_RCC_PLL_IsReady() != 1) {}

	LL_RCC_SetAHBPrescaler(LL_RCC_SYSCLK_DIV_1);
	LL_RCC_SetSysClkSource(LL_RCC_SYS_CLKSOURCE_PLL);
	while (LL_RCC_GetSysClkSource() != LL_RCC_SYS_CLKSOURCE_STATUS_PLL) {}

	LL_RCC_SetAPB1Prescaler(LL_RCC_APB1_DIV_1);
	// Use BSP::InitSystemTick()
	// LL_Init1msTick(64000000);
	LL_SetSystemCoreClock(64000000);
}



void BSP::InitSystemTick(uint32 ms, uint32 tickPriority) {
	SysTick_Config(System::GetCoreClock() / (1000U / ((1000U * ms) / 1000U)));
	NVIC_SetPriority(SysTick_IRQn, tickPriority);
}



void BSP::InitAdapterPeripheryEvents() {
	consoleSerial.beforePeripheryInit = []() {
		LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_USART1);

		AGPIO::AlternateInit({ GPIOA, 9,  1, AGPIO::Pull::Up, AGPIO::Speed::VeryHigh }); // TX
		AGPIO::AlternateInit({ GPIOA, 10, 1, AGPIO::Pull::Up, AGPIO::Speed::VeryHigh }); // RX

		NVIC_SetPriority(USART1_IRQn, 0);
		NVIC_EnableIRQ(USART1_IRQn);

		return ResultStatus::ok;
	};
}



void BSP::InitIO() {
	ledPin.Reset().SetParameters({ AGPIO::Mode::OpenDrain });
}
