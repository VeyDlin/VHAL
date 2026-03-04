#pragma once


// ==================== STM32F4 ====================
#if defined(VHAL_STM32_F4)

	#include "stm32f4xx_ll_rcc.h"
	#include "stm32f4xx_ll_bus.h"
	#include "stm32f4xx_ll_system.h"
	#include "stm32f4xx_ll_cortex.h"
	#include "stm32f4xx_ll_utils.h"
	#include "stm32f4xx_ll_pwr.h"
	#include "stm32f4xx_ll_gpio.h"
	#include "stm32f4xx_ll_exti.h"
	#include "cmsis_gcc.h"

	#ifdef VHAL_STM32_F4_UART
		#include "stm32f4xx_ll_usart.h"
	#endif

	#ifdef VHAL_STM32_F4_SPI
		#include "stm32f4xx_ll_spi.h"
	#endif

	#ifdef VHAL_STM32_F4_I2C
		#include "stm32f4xx_ll_i2c.h"
	#endif

	#ifdef VHAL_STM32_F4_TIM
		#include "stm32f4xx_ll_tim.h"
	#endif

	#ifdef VHAL_STM32_F4_ADC
		#include "stm32f4xx_ll_adc.h"
	#endif

	#ifdef VHAL_STM32_F4_DAC
		#include "stm32f4xx_ll_dac.h"
	#endif

	#ifdef VHAL_STM32_F4_DMA
		#include "stm32f4xx_ll_dma.h"
	#endif

	#ifdef VHAL_STM32_F4_IWDG
		#include "stm32f4xx_ll_iwdg.h"
	#endif


// ==================== STM32G0 ====================
#elif defined(VHAL_STM32_G0)

	#include "stm32g0xx_ll_rcc.h"
	#include "stm32g0xx_ll_bus.h"
	#include "stm32g0xx_ll_system.h"
	#include "stm32g0xx_ll_cortex.h"
	#include "stm32g0xx_ll_utils.h"
	#include "stm32g0xx_ll_pwr.h"
	#include "stm32g0xx_ll_gpio.h"
	#include "stm32g0xx_ll_exti.h"
	#include "cmsis_gcc.h"

	#ifdef VHAL_STM32_G0_I2C
		#include "stm32g0xx_ll_i2c.h"
	#endif

	#ifdef VHAL_STM32_G0_TIM
		#include "stm32g0xx_ll_tim.h"
	#endif

	#ifdef VHAL_STM32_G0_ADC
		#include "stm32g0xx_ll_adc.h"
	#endif

	#ifdef VHAL_STM32_G0_DAC
		#include "stm32g0xx_ll_dac.h"
	#endif

	#ifdef VHAL_STM32_G0_DMA
		#include "stm32g0xx_ll_dma.h"
	#endif

	#ifdef VHAL_STM32_G0_IWDG
		#include "stm32g0xx_ll_iwdg.h"
	#endif


// ==================== STM32G4 ====================
#elif defined(VHAL_STM32_G4)

	#include "stm32g4xx_ll_rcc.h"
	#include "stm32g4xx_ll_bus.h"
	#include "stm32g4xx_ll_system.h"
	#include "stm32g4xx_ll_cortex.h"
	#include "stm32g4xx_ll_utils.h"
	#include "stm32g4xx_ll_pwr.h"
	#include "stm32g4xx_ll_gpio.h"
	#include "stm32g4xx_ll_exti.h"
	#include "cmsis_gcc.h"

	#ifdef VHAL_STM32_G4_UART
		#include "stm32g4xx_ll_usart.h"
	#endif

	#ifdef VHAL_STM32_G4_TIM
		#include "stm32g4xx_ll_tim.h"
	#endif

	#ifdef VHAL_STM32_G4_ADC
		#include "stm32g4xx_ll_adc.h"
	#endif

	#ifdef VHAL_STM32_G4_DAC
		#include "stm32g4xx_ll_dac.h"
	#endif

	#ifdef VHAL_STM32_G4_COMP
		#include "stm32g4xx_ll_comp.h"
	#endif

	#ifdef VHAL_STM32_G4_IWDG
		#include "stm32g4xx_ll_iwdg.h"
	#endif

#else
	#error "VHAL: Define VHAL_STM32_F4, VHAL_STM32_G0, or VHAL_STM32_G4"
#endif


#if defined(USE_FULL_ASSERT)
	#include "stm32_assert.h"
#endif
