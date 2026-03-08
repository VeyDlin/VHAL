#include "DeviceIrq.h"
#include <BSP.h>


void USART1_IRQHandler() {
	BSP::consoleSerial.IrqHandler();
}
