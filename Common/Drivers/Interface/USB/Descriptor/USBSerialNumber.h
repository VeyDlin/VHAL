#pragma once
#include <Drivers/Interface/USB/Descriptor/USBHexStringDescriptor.h>
#include <Utilities/Result/ResultStatus.h>
#include <cstddef>
#include <span>


// Serial number string descriptor for a unique ID, such as the MCU's unique ID register: the bytes of the ID as upper-case hex
// text, two characters per byte, UTF-16LE (USB 2.0 section 9.6.7). The same ID always gives the same text, so a host that
// remembers the device by its serial number finds it again after a reboot. Unlike the builders, this one is not restricted to
// constant expressions: an application calls it at run time with its own ID and gives the result to USBDevice::SetSerialNumber().
// It is MakeUSBHexStringDescriptor() under the serial number's name; the rules are the same.
//
// descriptor must hold 2 + 4 * uniqueId.size() bytes. The call returns invalidParameter when it does not, when the ID is empty (every
// board with an empty ID would then report the same empty serial), and when that size would exceed the 255 bytes bLength can describe.
constexpr ResultStatus MakeUSBSerialNumberDescriptor(std::span<const uint8> uniqueId, std::span<uint8> descriptor) {
	return MakeUSBHexStringDescriptor(uniqueId, descriptor);
}
