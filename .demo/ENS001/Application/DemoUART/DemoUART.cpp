#include "DemoUART.h"


namespace DemoUART {

void Run() {
	// Configure GPIO2 (RX) and GPIO3 (TX) as UART0 alternate function
	AGPIO::AlternateInit({
		.port = CMSDK_GPIO,
		.pin = Pin::Uart0Rx,
		.alternate = Pin::Uart0Alt,
		.pull = AGPIO::Pull::Up
	});

	AGPIO::AlternateInit({
		.port = CMSDK_GPIO,
		.pin = Pin::Uart0Tx,
		.alternate = Pin::Uart0Alt
	});

	// Configure UART0: 115200 8N1
	BSP::uart0.SetParameters({
		.baudRate = 115200,
		.stopBits = AUART::StopBits::B1,
		.parity = AUART::Parity::None,
		.mode = AUART::Mode::TxRx
	});

	NVIC_EnableIRQ(UART0_IRQn);

	// Send greeting
	BSP::uart0.WriteString("ENS001 UART Echo Demo\r\n");

	// Echo loop (polling)
	while (true) {
		uint8 byte = 0;
		if (BSP::uart0.ReadArray(&byte) == ResultStatus::ok) {
			BSP::uart0.Write(byte);
		}
	}
}

}
