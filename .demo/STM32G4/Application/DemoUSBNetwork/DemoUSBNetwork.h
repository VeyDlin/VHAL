#pragma once
#include <BSP.h>


namespace DemoUSBNetwork {
	// The network demo: a CDC-NCM Ethernet device that answers ARP, ICMP echo and DHCP for 192.168.7.1 (the lease is 192.168.7.2). It
	// starts the USB device, pumps it and the network responder from the main loop, and prints one status line per second to
	// System::console. It does not return. consoleStatus is the result of BSP::Init() for USART1.
	void Run(ResultStatus consoleStatus);
}
