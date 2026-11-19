#pragma once
#include <array>
#include <cstddef>
#include <span>
#include <Drivers/Interface/USB/Descriptor/USBConfigurationDescriptor.h>
#include <Drivers/Interface/USB/Descriptor/USBDescriptorUtility.h>


// The rules that join the device descriptor with the configuration and the BOS. The device builder cannot see the others and
// the configuration builder cannot see the device, so these checks are the one place those rules are enforced. They return
// bool for a static_assert, which names the rule when it fails:
//
//   static_assert(USBDeviceClassMatchesConfiguration(device, configuration), "...");


// The device class (bDeviceClass, bDeviceSubClass, bDeviceProtocol are bytes 4 to 6 of the device descriptor) announces
// interface associations exactly when the configuration holds one: 0xEF, 0x02, 0x01 with associations (USB Interface
// Association Descriptor ECN), and 0x00, 0x00, 0x00 (class per interface) without them.
template<std::size_t ConfigurationSize>
constexpr bool USBDeviceClassMatchesConfiguration(
	const std::array<uint8, 18>& device,
	const std::array<uint8, ConfigurationSize>& configuration)
{
	bool associated = USBConfigurationUsesAssociation(configuration);
	bool associationClass = device[4] == 0xEF && device[5] == 0x02 && device[6] == 0x01;
	bool classPerInterface = device[4] == 0x00 && device[5] == 0x00 && device[6] == 0x00;
	return associated ? associationClass : classPerInterface;
}


// A BOS descriptor is defined for bcdUSB 0x0201 and later (USB 2.0 link power management ECN). The lower bound of 0x0201 is used
// for any device with a BOS: Linux requests the BOS only from such devices, and the Windows threshold is not verified here.
constexpr bool USBDeviceSupportsBos(const std::array<uint8, 18>& device) {
	return USBDescriptorRead16(device, 2) >= 0x0201;
}


// The pairing rule between the device and its BOS: a device that has a BOS must announce bcdUSB 0x0201 or later. The BOS builder
// cannot see the device, so the application checks the pair here. A device without a BOS has no such requirement.
constexpr bool USBDeviceBosMatches(const std::array<uint8, 18>& device, std::span<const uint8> bos) {
	return bos.empty() || USBDeviceSupportsBos(device);
}
