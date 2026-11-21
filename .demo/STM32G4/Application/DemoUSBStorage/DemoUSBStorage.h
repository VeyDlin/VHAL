#pragma once
#include <BSP.h>


namespace DemoUSBStorage {
	// The mass storage demo: a RAM disk with a FAT12 volume, on a USB mass storage device that the host mounts with no formatting. It
	// does not return. consoleStatus is the result of BSP::Init() for USART1.
	void Run(ResultStatus consoleStatus);
}
