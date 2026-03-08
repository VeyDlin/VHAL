#include "SystemIrq.h"
#include <BSP.h>



void NMI_Handler() {

}



void HardFault_Handler() {
	System::CriticalError("HardFault");
}



void SVC_Handler() {
	OS::RTOS::HandleSvcInterrupt();
}



void PendSV_Handler() {
	OS::RTOS::HandlePendSvInterrupt();
}



void SysTick_Handler() {
	System::TickHandler();
	OS::RTOS::HandleSysTickInterrupt();
}



extern "C" void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName) {
	System::CriticalError("Stack overflow");
}
