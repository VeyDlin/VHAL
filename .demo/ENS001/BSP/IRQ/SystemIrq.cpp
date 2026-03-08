#include "SystemIrq.h"
#include <VHAL.h>


void NMI_Handler() {

}


void HardFault_Handler() {
	System::CriticalError("HardFault");
}


void SysTick_Handler() {
	System::TickHandler();
}
