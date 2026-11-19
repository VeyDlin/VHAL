#pragma once
#include <BSP.h>


namespace DemoUSBBench {
	// The USB benchmark device: starts the USB device, pumps it from the main loop, handles the Reconnect request and prints
	// one status line per second to System::console. It does not return. consoleStatus is the result of BSP::Init() for USART1.
	void Run(ResultStatus consoleStatus);
}
