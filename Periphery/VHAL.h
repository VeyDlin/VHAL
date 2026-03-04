#pragma once
#include <VHALConfig.h>
#include <System/System.h>


#if defined(VHAL_STM32)
    #include <Adapter/Port/STM32/Platform.h>
	#include <Adapter/Port/STM32/Adapter.h>
#elif defined(VHAL_ENS)
    #include <Adapter/Port/ENS/Platform.h>
	#include <Adapter/Port/ENS/Adapter.h>
#else
	#error "VHAL: Define VHAL_STM32 or VHAL_ENS in your VHALConfig.h"
#endif


#if defined(VHAL_RTOS)
	#include <OS/Port/Wrapper.h>
	#include <OS/Adapter.h>
#endif
