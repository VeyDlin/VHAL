#include <IRQ/DeviceIrq.h>
#include <BSP.h>


// The USB controller raises its interrupts on USB_LP. USB_HP is kept disabled in the NVIC (see BSP::InitUsbClock()), so this
// handler never runs. It is a placeholder that gives the vector table the name the application expects. Enabling USB_HP as
// well would serve the same flags from two vectors, so it must not be enabled.
extern "C" void USB_LP_IRQHandler(void) {
	BSP::usbAdapter.IrqHandler();
}


extern "C" void USB_HP_IRQHandler(void) {
	BSP::usbAdapter.IrqHandler();
}


// USART1 transmit of the console text. The handler is the adapter's, which calls the transmit-complete callback set in BSP.
extern "C" void USART1_IRQHandler(void) {
	BSP::consoleSerial.IrqHandler();
}
