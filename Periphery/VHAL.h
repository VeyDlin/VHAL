#pragma once
#include <VHALConfig.h>
#include <System/System.h>


#if defined(VHAL_STM32)
    #include <Adapter/Port/STM32/Platform.h>
	#include <Adapter/Port/STM32/Adapter.h>
#elif defined(VHAL_ENS)
    #include <Adapter/Port/ENS/Platform.h>
	#include <Adapter/Port/ENS/Adapter.h>
#elif defined(VHAL_ESP32)
    #include <Adapter/Port/ESP32/Platform.h>
	#include <Adapter/Port/ESP32/Adapter.h>
#else
	#error "VHAL: Define VHAL_STM32, VHAL_ENS, or VHAL_ESP32 in your VHALConfig.h"
#endif


#if defined(VHAL_RTOS)
	#include <OS/Port/Wrapper.h>
	#include <OS/Adapter.h>
#endif


#ifdef VHAL_SYSTEM_CONSOLE
	#include <Utilities/Console/Console.h>
#endif