#pragma once
#include <Application.h>


// ESP-IDF counts the task stack in bytes, so it is larger than in the STM32 demo.
class MainTask : public ThreadStatic<4096> {
public:
	void Execute() override {
		BSP::consoleSerial.SetParameters({
			.baudRate = 115200
		});

		BSP::consoleSerial.WriteString("Hello from ESP32!\r\n");

		while (true) {
			BSP::ledPin.Toggle();
			BSP::consoleSerial.WriteString("Blink\r\n");
			Sleep(1s);
		}
	}
};
