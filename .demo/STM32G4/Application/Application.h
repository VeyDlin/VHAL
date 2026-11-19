#pragma once
#include <BSP.h>


// Chooses the demo that runs on the board and starts it. The G4 board will host more demos later (for example a HID composite
// device); each one is picked here, so main() stays the same.
class Application {
public:
	// consoleStatus is the result of BSP::Init() for USART1. Without a working console the demo still runs.
	static void Run(ResultStatus consoleStatus);
};
