#pragma once


#if defined(VHAL_ENS_001)

	#include "CMSDK_CM0.h"
	#define assert_param(expr) ((void)0U)

	#include <Adapter/Port/ENS/001/GPIOAdapter001.h>

	#ifdef VHAL_ENS_001_UART
		#include <Adapter/Port/ENS/001/UARTAdapter001.h>
	#endif

	#ifdef VHAL_ENS_001_SPI
		#include <Adapter/Port/ENS/001/SPIAdapter001.h>
	#endif

	#ifdef VHAL_ENS_001_I2C
		#include <Adapter/Port/ENS/001/I2CAdapter001.h>
	#endif

	#ifdef VHAL_ENS_001_TIM
		#include <Adapter/Port/ENS/001/TIMAdapter001.h>
	#endif

	#ifdef VHAL_ENS_001_ADC
		#include <Adapter/Port/ENS/001/ADCAdapter001.h>
	#endif

	#ifdef VHAL_ENS_001_IWDG
		#include <Adapter/Port/ENS/001/IWDGAdapter001.h>
	#endif

	#ifdef VHAL_ENS_001_WAVEGEN
		#include <Adapter/Port/ENS/001/WaveGeneratorAdapter001.h>
	#endif

	#ifdef VHAL_ENS_001_BOOST
		#include <Adapter/Port/ENS/001/BoostAdapter001.h>
	#endif

	#ifdef VHAL_ENS_001_FLASH
		#include <Adapter/Port/ENS/001/FLASHAdapter001.h>
	#endif

	#ifdef VHAL_ENS_001_COMP
		#include <Adapter/Port/ENS/001/COMPAdapter001.h>
	#endif

	#ifdef VHAL_ENS_001_PGA
		#include <Adapter/Port/ENS/001/PGAAdapter001.h>
	#endif

	#ifdef VHAL_ENS_001_PMU
		#include <Adapter/Port/ENS/001/PMUAdapter001.h>
	#endif

#else
	#error "VHAL: Define VHAL_ENS_001"
#endif
