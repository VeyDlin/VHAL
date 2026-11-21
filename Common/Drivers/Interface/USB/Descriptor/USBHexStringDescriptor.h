#pragma once
#include <Drivers/Interface/USB/USBProtocol.h>
#include <Utilities/Result/ResultStatus.h>
#include <cstddef>
#include <span>


// String descriptor of the hex text of a byte span: each byte as two upper-case hex digits, UTF-16LE (USB 2.0 section 9.6.7). The same
// bytes always give the same text. Use it for a run-time string whose value the device knows only at run time, such as a serial number
// from the MCU's unique ID, or a 6-byte MAC address for iMACAddress of CDC-NCM and CDC-ECM. A MAC address gives the 12 digits that
// iMACAddress asks for, for example 02 00 00 AB CD EF gives "020000ABCDEF". Unlike the builders, this one is not restricted to
// constant expressions: an application calls it at run time and gives the result to USBDevice::SetSerialNumber() or
// USBDevice::SetRuntimeString().
//
// descriptor must hold 2 + 4 * bytes.size() bytes. The call returns invalidParameter when it does not, when bytes is empty (every
// device with an empty value would then report the same empty string), and when that size would exceed the 255 bytes bLength can
// describe.
constexpr ResultStatus MakeUSBHexStringDescriptor(std::span<const uint8> bytes, std::span<uint8> descriptor) {
	constexpr char HexDigits[] = "0123456789ABCDEF";
	constexpr std::size_t MaxDescriptorSize = 255;
	std::size_t size = 2 + 4 * bytes.size();
	if (bytes.empty() || size > MaxDescriptorSize || descriptor.size() < size) {
		return ResultStatus::invalidParameter;
	}

	descriptor[0] = static_cast<uint8>(size);
	descriptor[1] = static_cast<uint8>(USBDescriptorType::String);
	std::size_t offset = 2;
	for (uint8 value : bytes) {
		descriptor[offset] = static_cast<uint8>(HexDigits[value >> 4]);
		descriptor[offset + 1] = 0;
		descriptor[offset + 2] = static_cast<uint8>(HexDigits[value & 0x0F]);
		descriptor[offset + 3] = 0;
		offset += 4;
	}

	return ResultStatus::ok;
}
