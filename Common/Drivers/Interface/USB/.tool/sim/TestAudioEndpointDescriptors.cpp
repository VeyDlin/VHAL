#include <VHAL.h>
#include "HidTestDevices.h"
#include "SimAssert.h"
#include <Drivers/Interface/USB/Descriptor/USBConfigurationDescriptor.h>
#include <array>
#include <cstddef>
#include <span>


// The two endpoint forms of the configuration: the standard 7-byte descriptor (USB 2.0 section 9.6.6) and the audio-class 9-byte
// descriptor (bRefresh and bSynchAddress added, USB Audio 1.0 section 4.8.1.1). The expected bytes are written out by hand. The
// 7-byte form keeps every rule it had. The 9-byte form is accepted for bulk and isochronous endpoints: a bulk one only with both added
// fields zero, an isochronous one with any bRefresh and bSynchAddress. A 7-byte and a 9-byte endpoint can share one configuration. The
// refused forms are BuilderErrors snippets.


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


// Isochronous endpoints in the standard form: bmAttributes carries 01 for the type, the sync type in bits 3..2 and the usage in bits 5..4.
// A data endpoint with asynchronous sync is 0x05. A feedback endpoint (usage 1) with no sync is 0x11. The packet size is 1023 at most,
// and the interval is 1.
void TestIsochronousEndpointBytes() {
	constexpr std::array<uint8, 7> dataIn = MakeUSBEndpointDescriptor(0x81, USBEndpointType::Isochronous, 192, 1, USBIsoSync::Asynchronous);
	constexpr uint8 expectedDataIn[] = {
		0x07, 0x05, 0x81, 0x05, 0xC0, 0x00, 0x01
	};

	SIM_EXPECT(SimHost::SameBytes(std::span<const uint8>(dataIn), std::span<const uint8>(expectedDataIn)));

	constexpr std::array<uint8, 7> feedbackIn = MakeUSBEndpointDescriptor(0x82, USBEndpointType::Isochronous, 3, 1, USBIsoSync::None, USBIsoUsage::Feedback);
	constexpr uint8 expectedFeedbackIn[] = {
		0x07, 0x05, 0x82, 0x11, 0x03, 0x00, 0x01
	};

	SIM_EXPECT(SimHost::SameBytes(std::span<const uint8>(feedbackIn), std::span<const uint8>(expectedFeedbackIn)));

	constexpr std::array<uint8, 7> largestOut = MakeUSBEndpointDescriptor(0x01, USBEndpointType::Isochronous, 1023, 1);
	constexpr uint8 expectedLargestOut[] = {
		0x07, 0x05, 0x01, 0x01, 0xFF, 0x03, 0x01
	};

	SIM_EXPECT(SimHost::SameBytes(std::span<const uint8>(largestOut), std::span<const uint8>(expectedLargestOut)));
}


// The audio form of an isochronous endpoint: bRefresh 0, and bSynchAddress names the feedback endpoint 0x82 (an IN address).
void TestIsochronousAudioEndpointBytes() {
	constexpr std::array<uint8, 9> audioIn = MakeUSBAudioEndpointDescriptor(0x81, USBEndpointType::Isochronous, 192, 1, USBIsoSync::Asynchronous, USBIsoUsage::Data, 0, 0x82);
	constexpr uint8 expectedAudioIn[] = {
		0x09, 0x05, 0x81, 0x05, 0xC0, 0x00, 0x01, 0x00, 0x82
	};

	SIM_EXPECT(SimHost::SameBytes(std::span<const uint8>(audioIn), std::span<const uint8>(expectedAudioIn)));
}


// A configuration with one isochronous IN endpoint in the audio form, without a feedback endpoint. wTotalLength 27 (0x1B).
void TestIsochronousAudioEndpointInConfiguration() {
	constexpr std::array<uint8, 9> endpoint = MakeUSBAudioEndpointDescriptor(0x81, USBEndpointType::Isochronous, 200, 1);
	constexpr std::array<uint8, 27> configuration = MakeUSBConfigurationDescriptor(1, 0, false, false, 50, VendorInterface, endpoint);
	constexpr uint8 expected[] = {
		0x09, 0x02, 0x1B, 0x00, 0x01, 0x01, 0x00, 0x80, 0x32,	// Configuration: one interface, 100 mA
		0x09, 0x04, 0x00, 0x00, 0x01, 0xFF, 0x00, 0x00, 0x00,	// Interface 0: one endpoint, vendor class
		0x09, 0x05, 0x81, 0x01, 0xC8, 0x00, 0x01, 0x00, 0x00	// Isochronous IN 0x81, 200 bytes, 9-byte form, no feedback
	};

	SIM_EXPECT(SimHost::SameBytes(std::span<const uint8>(configuration), std::span<const uint8>(expected)));
}


int main() {
	TestStandardEndpointBytes();
	TestAudioEndpointBytes();
	TestAudioEndpointInConfiguration();
	TestBothFormsInOneConfiguration();
	TestIsochronousEndpointBytes();
	TestIsochronousAudioEndpointBytes();
	TestIsochronousAudioEndpointInConfiguration();
	return SimTest::Finish("TestAudioEndpointDescriptors");
}
