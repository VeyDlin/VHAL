#pragma once
#include "Platform.h"

#include <Adapter/Port/ESP32/Common/GPIOAdapterESP.h>

#ifdef VHAL_ESP32_UART
	#include <Adapter/Port/ESP32/Common/UARTAdapterESP.h>
#endif

#ifdef VHAL_ESP32_SPI
	#include <Adapter/Port/ESP32/Common/SPIAdapterESP.h>
#endif

#ifdef VHAL_ESP32_I2C
	#include <Adapter/Port/ESP32/Common/I2CAdapterESP.h>
#endif

#ifdef VHAL_ESP32_ADC
	#include <Adapter/Port/ESP32/Common/ADCAdapterESP.h>
#endif

#ifdef VHAL_ESP32_DAC
	#if !SOC_DAC_SUPPORTED
		#error "VHAL: DAC is not supported on this ESP32 chip"
	#endif
	#include <Adapter/Port/ESP32/Common/DACAdapterESP.h>
#endif

#ifdef VHAL_ESP32_I2S
	#include <Adapter/Port/ESP32/Common/I2SAdapterESP.h>
#endif

#ifdef VHAL_ESP32_GPTIMER
	#include <Adapter/Port/ESP32/Common/GPTimerAdapterESP.h>
#endif

#ifdef VHAL_ESP32_IWDG
	#include <Adapter/Port/ESP32/Common/IWDGAdapterESP.h>
#endif

#ifdef VHAL_ESP32_LEDC
	#include <Adapter/Port/ESP32/Common/LEDCAdapterESP.h>
#endif

#ifdef VHAL_ESP32_MCPWM
	#include <Adapter/Port/ESP32/Common/MCPWMAdapterESP.h>
#endif

#ifdef VHAL_ESP32_DSI
	#include <Adapter/Port/ESP32/Common/DSIAdapterESP.h>
#endif

#ifdef VHAL_ESP32_PPA
	#include <Adapter/Port/ESP32/Common/PPAAdapterESP.h>
#endif

#ifdef VHAL_ESP32_LDO
	#include <Adapter/Port/ESP32/Common/LDOAdapterESP.h>
#endif

#ifdef VHAL_ESP32_WIFI
	#include <Adapter/Port/ESP32/Common/WiFiAdapterESP.h>
#endif

#ifdef VHAL_ESP32_BLE
	#include <Adapter/Port/ESP32/Common/BLEAdapterESP.h>
#endif

#ifdef VHAL_ESP32_SOCKET
	#include <Adapter/Port/ESP32/Common/SocketAdapterESP.h>
#endif
