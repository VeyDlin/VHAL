#include "DeviceIrq.h"
#include <BSP.h>


void UART0_Handler() {
	BSP::uart0.IrqHandler();
}


void ADC_Handler() {
	BSP::adc.IrqHandler();
}


void WG_DRV_Handler() {
	BSP::waveGen.IrqHandler();
}


void TIMER0_Handler() {
	BSP::timer0.IrqHandler();
}
