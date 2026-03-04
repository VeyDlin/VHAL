#pragma once
#include "Platform.h"


// ==================== STM32F4 ====================
#if defined(VHAL_STM32_F4)

	#include <Adapter/Port/STM32/F4/GPIOAdapterF4.h>

	#ifdef VHAL_STM32_F4_UART
		#include <Adapter/Port/STM32/F4/UARTAdapterF4.h>
	#endif

	#ifdef VHAL_STM32_F4_SPI
		#include <Adapter/Port/STM32/F4/SPIAdapterF4.h>
	#endif

	#ifdef VHAL_STM32_F4_I2C
		#include <Adapter/Port/STM32/F4/I2CAdapterF4.h>
	#endif

	#ifdef VHAL_STM32_F4_TIM
		#include <Adapter/Port/STM32/F4/TIMAdapterF4.h>
	#endif

	#ifdef VHAL_STM32_F4_ADC
		#include <Adapter/Port/STM32/F4/ADCAdapterF4.h>
	#endif

	#ifdef VHAL_STM32_F4_DAC
		#include <Adapter/Port/STM32/F4/DACAdapterF4.h>
	#endif

	#ifdef VHAL_STM32_F4_IWDG
		#include <Adapter/Port/STM32/F4/IWDGAdapterF4.h>
	#endif

	#ifdef VHAL_STM32_F4_FLASH
		#include <Adapter/Port/STM32/F4/FLASHAdapterF4.h>
	#endif


// ==================== STM32G0 ====================
#elif defined(VHAL_STM32_G0)

	#include <Adapter/Port/STM32/G0/GPIOAdapterG0.h>

	#ifdef VHAL_STM32_G0_DMA
		#include <Adapter/Port/STM32/G0/DMAAdapterG0.h>
	#endif

	#ifdef VHAL_STM32_G0_I2C
		#include <Adapter/Port/STM32/G0/I2CAdapterG0.h>
	#endif

	#ifdef VHAL_STM32_G0_TIM
		#include <Adapter/Port/STM32/G0/TIMAdapterG0.h>
	#endif

	#ifdef VHAL_STM32_G0_ADC
		#include <Adapter/Port/STM32/G0/ADCAdapterG0.h>
	#endif

	#ifdef VHAL_STM32_G0_DAC
		#include <Adapter/Port/STM32/G0/DACAdapterG0.h>
	#endif

	#ifdef VHAL_STM32_G0_IWDG
		#include <Adapter/Port/STM32/G0/IWDGAdapterG0.h>
	#endif


// ==================== STM32G4 ====================
#elif defined(VHAL_STM32_G4)

	#include <Adapter/Port/STM32/G4/GPIOAdapterG4.h>

	#ifdef VHAL_STM32_G4_UART
		#include <Adapter/Port/STM32/G4/UARTAdapterG4.h>
	#endif

	#ifdef VHAL_STM32_G4_TIM
		#include <Adapter/Port/STM32/G4/TIMAdapterG4.h>
	#endif

	#ifdef VHAL_STM32_G4_ADC
		#include <Adapter/Port/STM32/G4/ADCAdapterG4.h>
	#endif

	#ifdef VHAL_STM32_G4_DAC
		#include <Adapter/Port/STM32/G4/DACAdapterG4.h>
	#endif

	#ifdef VHAL_STM32_G4_COMP
		#include <Adapter/Port/STM32/G4/COMPAdapterG4.h>
	#endif

	#ifdef VHAL_STM32_G4_IWDG
		#include <Adapter/Port/STM32/G4/IWDGAdapterG4.h>
	#endif

#endif
