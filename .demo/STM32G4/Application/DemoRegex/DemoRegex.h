#pragma once
#include <BSP.h>


namespace DemoRegex {
	// The regex board check: runs the fixtures of Common/Utilities/Regex/.tool/sim/fixtures/Board.inc against the matcher, paints the free
	// RAM below the stack to measure the stack that the matching took, and prints the result to System::console. It does not return.
	// consoleStatus is the result of BSP::Init() for USART1. The variant that also calls Find is built with VHAL_REGEX_FIND defined. vhal.cmake
	// forces CMAKE_CXX_FLAGS, so the define goes in CMAKE_CXX_FLAGS_RELEASE: -DCMAKE_CXX_FLAGS_RELEASE="-O3 -DNDEBUG -DVHAL_REGEX_FIND".
	void Run(ResultStatus consoleStatus);
}
