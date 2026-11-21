#pragma once
#include <BSP.h>


// Starts the demo that the build selected with VHAL_DEMO (see CMakeLists.txt). Only that demo is compiled, so main() stays the same
// whichever demo runs.
class Application {
public:
	// consoleStatus is the result of BSP::Init() for USART1. Without a working console the demo still runs.
	static void Run(ResultStatus consoleStatus);
};
