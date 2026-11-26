#pragma once
#include <array>
#include <Adapter/USBDeviceAdapter.h>
#include <Drivers/Interface/USB/Descriptor/USBDescriptorUtility.h>
#include <Drivers/Interface/USB/USBProtocol.h>
#include <Utilities/CompileTime/Require.h>


// Device descriptor, 18 bytes (USB 2.0 section 9.6.1). Every argument is required and none has a default: the identities
// (idVendor, idProduct, bcdDevice, the string indexes) belong to the application. bMaxPacketSize0 must be the control packet
// size of the controller, because the host sizes its first GET_DESCRIPTOR from this byte. This builder cannot see the
// configuration or the BOS, so the rules that join them (device class, bcdUSB) are in USBDescriptorComposition.h.
constexpr std::array<uint8, 18> MakeUSBDeviceDescriptor(
	uint16 bcdUSB,
	uint8 deviceClass,
	uint8 deviceSubClass,
	uint8 deviceProtocol,
	uint8 maxPacketSize0,
	uint16 idVendor,
	uint16 idProduct,
	uint16 bcdDevice,
	uint8 manufacturerIndex,
	uint8 productIndex,
	uint8 serialNumberIndex,
	uint8 numConfigurations)
{
	Require<"USB: bcdUSB is USB 2.0 or later">(bcdUSB >= 0x0200);
	Require<"USB: bMaxPacketSize0 must equal the adapter's control packet size">(maxPacketSize0 == USBDeviceAdapter::ControlMaxPacketSize);
	Require<"USB: the device core answers one configuration">(numConfigurations == 1);

	std::array<uint8, 18> bytes{};
	bytes[0] = 18;
	bytes[1] = static_cast<uint8>(USBDescriptorType::Device);
	USBDescriptorPut16(bytes, 2, bcdUSB);
	bytes[4] = deviceClass;
	bytes[5] = deviceSubClass;
	bytes[6] = deviceProtocol;
	bytes[7] = maxPacketSize0;
	USBDescriptorPut16(bytes, 8, idVendor);
	USBDescriptorPut16(bytes, 10, idProduct);
	USBDescriptorPut16(bytes, 12, bcdDevice);
	bytes[14] = manufacturerIndex;
	bytes[15] = productIndex;
	bytes[16] = serialNumberIndex;
	bytes[17] = numConfigurations;
	return bytes;
}
