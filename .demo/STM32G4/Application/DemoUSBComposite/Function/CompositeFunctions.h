#pragma once
#include <Drivers/Interface/USB/Class/CDC/CdcAcmFunction.h>
#include <Drivers/Interface/USB/Class/HID/HidFunction.h>
#include <Drivers/Interface/USB/Class/HID/HidKeyboard.h>
#include <Drivers/Interface/USB/Class/HID/HidMouse.h>
#include <Drivers/Interface/USB/Class/Vendor/VendorBulkFunction.h>
#include <cstddef>


namespace DemoUSBComposite {
	// Reports each keyboard and mouse function can queue. A key press and its release are two reports, so the keyboard types a
	// few characters per batch; the rest waits for the next main loop pass.
	constexpr std::size_t HidQueueDepth = 4;


	using KeyboardFunction = HidFunction<HidQueueDepth>;
	using Keyboard = HidKeyboard<HidQueueDepth>;
	using MouseFunction = HidFunction<HidQueueDepth>;
	using Mouse = HidMouse<HidQueueDepth>;


	// The serial port and the vendor channel. Each has a 512-byte transmit ring and a 512-byte receive ring, and transfers of four
	// packets (256 bytes). Each function's RAM is 1024 bytes of rings plus 768 bytes of transfer buffers (two IN, one OUT), 1792 bytes.
	using SerialPort = CdcAcmFunction<512, 512, 4>;
	using VendorChannel = VendorBulkFunction<512, 512, 4>;
}
