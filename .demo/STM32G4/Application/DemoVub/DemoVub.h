#pragma once
#include <BSP.h>


namespace DemoVub {
	// The VUB demo: four stacks with VUB::Reliable on one device, each echoing every payload it receives back to the sender. The stacks are
	// UART (USART1, COBS and CRC-16/MODBUS), CDC-ACM (COBS), vendor bulk (LengthPrefix<2>) and vendor HID (no framing). The device does not
	// return. consoleStatus is the result of BSP::Init() for USART1; USART1 is a data line in this demo, so the console is turned off.
	void Run(ResultStatus consoleStatus);
}
