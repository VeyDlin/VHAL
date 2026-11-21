#pragma once
#include <BSP.h>


namespace DemoUSBAudio {
	// The audio demo: a USB Audio 1.0 device (`1209:0006`) with a mono 48 kHz microphone and a stereo 48 kHz speaker. The microphone
	// sends a 1 kHz tone, or the speaker's audio while the speaker plays (a loopback). It starts the USB device, pumps it and the audio
	// from the main loop, and prints one status line per second to System::console. It does not return. consoleStatus is the result of
	// BSP::Init() for USART1.
	void Run(ResultStatus consoleStatus);
}
