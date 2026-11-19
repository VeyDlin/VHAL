#pragma once
#include <System/System.h>
#include <span>


// The descriptors the device core answers from, as the application gives them. The bytes come from the constexpr builders
// (Drivers/Interface/USB/Descriptor/) or from the application's own constant arrays. The core reads the spans and never
// copies or changes them, so the data must stay valid for the life of the device (static or constexpr storage).
// No value here has a default: identities (vendor and product IDs, strings, serial number) always come from the application.


// A vendor request answered with fixed bytes, such as the Microsoft OS 2.0 descriptor. The core matches an IN request to
// the device with type Vendor and this bRequest and wIndex, and also wValue when matchValue is set. It sends the bytes
// truncated to wLength. The core does not interpret the bytes.
struct USBVendorDescriptor {
	// bRequest: the application's vendor code.
	uint8 request;
	// wIndex the request must carry, for example 7 for the MS OS 2.0 descriptor.
	uint16 index;
	std::span<const uint8> bytes;
	// wValue the request must carry when matchValue is set, for example the URL index of WebUSB GET_URL. Without matchValue
	// every wValue matches, which is what the MS OS 2.0 descriptor needs.
	uint16 value = 0;
	bool matchValue = false;
};


struct USBDescriptorSet {
	// Device descriptor, 18 bytes. bMaxPacketSize0 (byte 7) must be 64.
	std::span<const uint8> device;
	// Full-speed configuration descriptor with its interface, endpoint and class descriptors. wTotalLength must equal the
	// size. This is the only configuration: GET_DESCRIPTOR(Configuration) with index 0 returns it.
	std::span<const uint8> configuration;
	// Indexed by string index. strings[0] is the language ID descriptor, strings[n] answers index n. Each entry is a
	// complete descriptor (bLength, bDescriptorType 3, UTF-16LE text). An index past the end stalls the request.
	std::span<const std::span<const uint8>> strings;
	// Optional BOS descriptor followed by its device capabilities. Empty: there is no BOS, and GET_DESCRIPTOR(BOS) stalls.
	std::span<const uint8> bos;
	// Vendor requests answered from fixed bytes. Empty: every vendor request to the device is stalled.
	std::span<const USBVendorDescriptor> vendorRequests;
	// True when iSerialNumber names a string the application supplies at run time through USBDevice::SetSerialNumber(); the strings table then holds only a placeholder at that index.
	bool serialNumberAtRuntime = false;
};
