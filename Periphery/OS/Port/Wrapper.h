#pragma once
#include <VHALConfig.h>

#if defined(VHAL_RTOS_FREERTOS)
	#include <OS/Port/FreeRTOS/Wrapper.h>
#else
	#error "VHAL: Define VHAL_RTOS_FREERTOS in your VHALConfig.h"
#endif
