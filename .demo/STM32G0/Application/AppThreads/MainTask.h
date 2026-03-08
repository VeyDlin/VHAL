#pragma once
#include <Application.h>



class MainTask: public ThreadStatic<128> {
public:
	virtual void Execute() override {
		BSP::consoleSerial.SetParameters({
			.baudRate = 115200
		});

		BSP::consoleSerial.WriteString("Hello from STM32G0!\r\n");

		while (true) {
			BSP::ledPin.Toggle();
			BSP::consoleSerial.WriteString("Blink\r\n");
			Sleep(1s);
		}
	}
};
