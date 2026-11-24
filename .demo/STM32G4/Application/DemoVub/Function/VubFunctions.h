#pragma once
#include <Drivers/Interface/USB/Class/CDC/CdcAcmFunction.h>
#include <Drivers/Interface/USB/Class/HID/HidFunction.h>
#include <Drivers/Interface/USB/Class/Vendor/VendorBulkFunction.h>
#include <cstddef>


namespace DemoVub {
	// Input and output reports the HID function can queue. Each one is a full-speed packet of 64 bytes.
	constexpr std::size_t HidQueueDepth = 4;


	// The serial port (CDC-ACM) and the vendor bulk channel. Each has a 512-byte transmit ring and a 512-byte receive ring, and transfers
	// of four packets, as the composite demo has them.
	using SerialPort = CdcAcmFunction<512, 512, 4>;
	using VendorChannel = VendorBulkFunction<512, 512, 4>;
	using VendorHid = HidFunction<HidQueueDepth>;
}
