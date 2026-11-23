#pragma once
#include <BSP.h>


namespace DemoModbus {
	// The Modbus RTU slave on USART1: slave address 1, 9600 baud, 8 data bits, even parity, one stop bit, plain TTL (no RS-485 pins).
	// The server answers in the main loop and does not return. consoleStatus is the result of BSP::Init() for USART1; it is kept in the
	// status block for SWD. The server starts when the demo's own SetParameters and Begin succeed, and halts when either fails.
	void Run(ResultStatus consoleStatus);
}
