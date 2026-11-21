#pragma once
#include <BSP.h>


namespace DemoUSBDfu {
	// The DFU demo: a DFU 1.1 mode device whose memory is the top 32 KB of flash, the region the demo image does not use. dfu-util
	// downloads to it and uploads from it. It starts the USB device, pumps it from the main loop, prints the byte count and CRC-32 of
	// each completed download, and prints one status line per second to System::console. It does not return. consoleStatus is the
	// result of BSP::Init() for USART1.
	void Run(ResultStatus consoleStatus);
}
