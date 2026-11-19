#include <IRQ/SystemIrq.h>
#include <BSP.h>


extern "C" void SysTick_Handler(void) {
	System::TickHandler();
}


extern "C" void HardFault_Handler(void) {
	System::CriticalError("HardFault");
}
