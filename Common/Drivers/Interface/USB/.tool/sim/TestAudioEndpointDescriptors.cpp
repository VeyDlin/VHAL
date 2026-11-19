#include <VHAL.h>
#include "HidTestDevices.h"
#include "SimAssert.h"
#include <Drivers/Interface/USB/Descriptor/USBConfigurationDescriptor.h>
#include <array>
#include <cstddef>
#include <span>


// The two endpoint forms of the configuration: the standard 7-byte descriptor (USB 2.0 section 9.6.6) and the audio-class 9-byte
// descriptor (bRefresh and bSynchAddress added, USB Audio 1.0 section 4.8.1.1). The expected bytes are written out by hand. The
// 7-byte form keeps every rule it had; the 9-byte form is accepted in a configuration only for bulk endpoints with both added fields
// zero, and a 7-byte and a 9-byte endpoint can share one configuration. The refused forms are BuilderErrors snippets.


namespace {


// The interface the configurations below hang their endpoints on: interface 0 of the vendor class, no string. The configuration sets
// its bNumEndpoints from the endpoints that follow it.
constexpr std::array<uint8, 9> VendorInterface = MakeUSBInterfaceDescriptor(0, 0, 0xFF, 0x00, 0x00, 0);


}


void TestStandardEndpointBytes() {
	constexpr std::array<uint8, 7> bulkIn = MakeUSBEndpointDescriptor(0x81, USBEndpointType::Bulk, 64, 0);
	constexpr uint8 expectedBulkIn[] = {
		0x07, 0x05, 0x81, 0x02, 0x40, 0x00, 0x00
	};

	SIM_EXPECT(SimHost::SameBytes(std::span<const uint8>(bulkIn), std::span<const uint8>(expectedBulkIn)));

	constexpr std::array<uint8, 7> interruptOut = MakeUSBEndpointDescriptor(0x02, USBEndpointType::Interrupt, 8, 4);
	constexpr uint8 expectedInterruptOut[] = {
		0x07, 0x05, 0x02, 0x03, 0x08, 0x00, 0x04
	};

	SIM_EXPECT(SimHost::SameBytes(std::span<const uint8>(interruptOut), std::span<const uint8>(expectedInterruptOut)));
}


void TestAudioEndpointBytes() {
	constexpr std::array<uint8, 9> bulkOut = MakeUSBAudioEndpointDescriptor(0x01, USBEndpointType::Bulk, 64, 0);
	constexpr uint8 expectedBulkOut[] = {
		0x09, 0x05, 0x01, 0x02, 0x40, 0x00, 0x00, 0x00, 0x00
	};

	SIM_EXPECT(SimHost::SameBytes(std::span<const uint8>(bulkOut), std::span<const uint8>(expectedBulkOut)));
}


// A configuration with one 9-byte bulk IN endpoint. wTotalLength 27 (0x1B): 9 + 9 + 9. bNumEndpoints of the interface is set to 1.
void TestAudioEndpointInConfiguration() {
	constexpr std::array<uint8, 9> endpoint = MakeUSBAudioEndpointDescriptor(0x81, USBEndpointType::Bulk, 64, 0);
	constexpr std::array<uint8, 27> configuration = MakeUSBConfigurationDescriptor(1, 0, false, false, 50, VendorInterface, endpoint);
	constexpr uint8 expected[] = {
		0x09, 0x02, 0x1B, 0x00, 0x01, 0x01, 0x00, 0x80, 0x32,	// Configuration: one interface, 100 mA
		0x09, 0x04, 0x00, 0x00, 0x01, 0xFF, 0x00, 0x00, 0x00,	// Interface 0: one endpoint, vendor class
		0x09, 0x05, 0x81, 0x02, 0x40, 0x00, 0x00, 0x00, 0x00	// Bulk IN 0x81, 9-byte form
	};

	SIM_EXPECT(SimHost::SameBytes(std::span<const uint8>(configuration), std::span<const uint8>(expected)));
}


// A 7-byte and a 9-byte endpoint in one configuration, on two interfaces. wTotalLength 43 (0x2B): 9 + 9 + 7 + 9 + 9.
void TestBothFormsInOneConfiguration() {
	constexpr std::array<uint8, 9> secondInterface = MakeUSBInterfaceDescriptor(1, 0, 0xFF, 0x00, 0x00, 0);
	constexpr std::array<uint8, 7> standardIn = MakeUSBEndpointDescriptor(0x81, USBEndpointType::Bulk, 64, 0);
	constexpr std::array<uint8, 9> audioOut = MakeUSBAudioEndpointDescriptor(0x02, USBEndpointType::Bulk, 64, 0);
	constexpr std::array<uint8, 43> configuration = MakeUSBConfigurationDescriptor(1, 0, false, false, 50, VendorInterface, standardIn, secondInterface, audioOut);
	constexpr uint8 expected[] = {
		0x09, 0x02, 0x2B, 0x00, 0x02, 0x01, 0x00, 0x80, 0x32,	// Configuration: two interfaces, 100 mA
		0x09, 0x04, 0x00, 0x00, 0x01, 0xFF, 0x00, 0x00, 0x00,	// Interface 0: one endpoint
		0x07, 0x05, 0x81, 0x02, 0x40, 0x00, 0x00,		// Bulk IN 0x81, 7-byte form
		0x09, 0x04, 0x01, 0x00, 0x01, 0xFF, 0x00, 0x00, 0x00,	// Interface 1: one endpoint
		0x09, 0x05, 0x02, 0x02, 0x40, 0x00, 0x00, 0x00, 0x00	// Bulk OUT 0x02, 9-byte form
	};

	SIM_EXPECT(SimHost::SameBytes(std::span<const uint8>(configuration), std::span<const uint8>(expected)));
}


int main() {
	TestStandardEndpointBytes();
	TestAudioEndpointBytes();
	TestAudioEndpointInConfiguration();
	TestBothFormsInOneConfiguration();
	return SimTest::Finish("TestAudioEndpointDescriptors");
}
