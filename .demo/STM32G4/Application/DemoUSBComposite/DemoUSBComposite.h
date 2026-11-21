#pragma once
#include <BSP.h>


namespace DemoUSBComposite {
	// The composite demo: a keyboard, a mouse, a serial command line and a vendor loopback on one device. It does not return. consoleStatus
	// is the result of BSP::Init() for USART1.
	void Run(ResultStatus consoleStatus);
}
