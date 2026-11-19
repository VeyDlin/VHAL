#pragma once
#include <Adapter/Port/STM32/Common/USBFsdev.h>


// USB device adapter on the STM32G431/G441/G471 "fsdev" peripheral. The register code is in Port/STM32/Common/USBFsdev.h;
// this file gives the family constants only.
//
// The application's BSP must provide a 48 MHz clock to the USB peripheral (HSI48 with CRS trimming from the USB
// start-of-frame), enable the USB peripheral clock and set the USB_LP_IRQn priority. The BSP code is on the USB adapter
// wiki page. Without the peripheral clock, Initialization() returns notReady.
//
// IrqHandler() serves every flag of the controller. The application calls it from the USB_LP_IRQn vector only: USB_HP_IRQn
// must stay disabled, or the same flags would be served from two vectors.


// The family constants of the fsdev block on STM32G4.
struct USBFsdevFamilyG4 {
	// Packet memory (1 KB at USB_PMAADDR), accessed as 16-bit halfwords at consecutive 2-byte addresses.
	static constexpr uintptr_t PmaBase = USB_PMAADDR;
	static constexpr uint16 PmaSize = 1024;
	static constexpr uint8 PmaElementStride = 2;
	static constexpr uint8 HardwareEndpointCount = 8;


	static bool IsClockEnabled() {
		return (RCC->APB1ENR1 & RCC_APB1ENR1_USBEN) != 0;
	}
};


using AUSB = class USBDeviceAdapterG4;


class USBDeviceAdapterG4 : public USBFsdev<USBFsdevFamilyG4> {
public:
	USBDeviceAdapterG4(USB_TypeDef* handle = USB)
		: USBFsdev(reinterpret_cast<uintptr_t>(handle)) {
	}
};
