#include <VHAL.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <span>
#include <Drivers/Interface/USB/Class/Vendor/USBVendorBuilder.h>
#include <Drivers/Interface/USB/Class/Vendor/VendorBulkFunction.h>
#include <Drivers/Interface/USB/Descriptor/Builder/USBConfigurationBuilder.h>
#include <Drivers/Interface/USB/Descriptor/Builder/USBDescriptors.h>
#include <Drivers/Interface/USB/Descriptor/WebUsb/USBWebUsbPlatformCapability.h>
#include <Drivers/Interface/USB/Descriptor/WebUsb/USBWebUsbUrlDescriptor.h>
#include <Drivers/Interface/USB/Device/USBDevice.h>
#include "Descriptors.h"
#include "SimAssert.h"
#include "SimHost.h"
#include "SimUSBDeviceAdapter.h"


// The WebUSB component: the platform capability and the URL descriptor against bytes written out from the specification
// layout, the BOS with and without the MS OS 2.0 set, and the device's answers to GET_URL. The vendor request table is matched
// on wValue as well as wIndex, so a GET_URL for an index that does not exist must stall.


namespace {


// Compares two byte sequences. A mismatch prints the first differing offset and both lengths.
void ExpectBytes(const char* name, std::span<const uint8> built, std::span<const uint8> expected) {
	bool same = built.size() == expected.size() && std::equal(built.begin(), built.end(), expected.begin());
	if (same) {
		return;
	}

	std::size_t firstDifference = 0;
	while (firstDifference < built.size() && firstDifference < expected.size() && built[firstDifference] == expected[firstDifference]) {
		firstDifference++;
	}

	std::fprintf(stderr, "%s differs: built %zu bytes, expected %zu, first difference at offset %zu\n",
		name, built.size(), expected.size(), firstDifference);
	SimTest::Expect(false, name, __FILE__, __LINE__);
}


// WebUSB platform capability for vendor code 0x42 and landing page index 1: bLength 24, type 16, capability type 5, reserved 0,
// the WebUSB UUID {3408B638-09A9-47A0-8BFD-A0768815B665} in wire byte order, bcdVersion 0x0100, bVendorCode, iLandingPage.
constexpr uint8 CapabilityBytes[] = {
	0x18, 0x10, 0x05, 0x00,
	0x38, 0xB6, 0x08, 0x34, 0xA9, 0x09, 0xA0, 0x47, 0x8B, 0xFD, 0xA0, 0x76, 0x88, 0x15, 0xB6, 0x65,
	0x00, 0x01, 0x42, 0x01
};


// URL descriptor of "https://example.com/": bLength 15, type 3, scheme 1 (https://), then the text after the prefix.
constexpr uint8 HttpsUrlBytes[] = {
	0x0F, 0x03, 0x01,
	0x65, 0x78, 0x61, 0x6D, 0x70, 0x6C, 0x65, 0x2E, 0x63, 0x6F, 0x6D, 0x2F
};


// URL descriptor of "http://a": scheme 0 (http://), one byte of text.
constexpr uint8 HttpUrlBytes[] = {
	0x04, 0x03, 0x00, 0x61
};


// URL descriptor of "vhal": no prefix, so scheme 255 and the whole text.
constexpr uint8 NoSchemeUrlBytes[] = {
	0x07, 0x03, 0xFF, 0x76, 0x68, 0x61, 0x6C
};


// BOS with the USB 2.0 extension left out, the MS OS 2.0 capability for vendor code 0x42 and set length 30, then the WebUSB
// capability for vendor code 0x43. wTotalLength 57 (5 + 28 + 24), two device capabilities.
constexpr uint8 BothBosBytes[] = {
	0x05, 0x0F, 0x39, 0x00, 0x02,
	0x1C, 0x10, 0x05, 0x00,
	0xDF, 0x60, 0xDD, 0xD8, 0x89, 0x45, 0xC7, 0x4C, 0x9C, 0xD2, 0x65, 0x9D, 0x9E, 0x64, 0x8A, 0x9F,
	0x00, 0x00, 0x03, 0x06, 0x1E, 0x00, 0x42, 0x00,
	0x18, 0x10, 0x05, 0x00,
	0x38, 0xB6, 0x08, 0x34, 0xA9, 0x09, 0xA0, 0x47, 0x8B, 0xFD, 0xA0, 0x76, 0x88, 0x15, 0xB6, 0x65,
	0x00, 0x01, 0x43, 0x01
};


// The device with MS OS 2.0 (vendor code 0x42) and WebUSB (vendor code 0x43) on one BOS. Both descriptors are asked for by one
// interface with WinUSB, so the MS OS 2.0 set is the device-level one that SimDescriptors::MsOs20Set holds.
using BothUsb = USBDescriptors<[] {
	return USBDeviceBuilder()
		.VendorId(0x1209)
		.ProductId(0x0001)
		.DeviceRelease(0x0100)
		.Manufacturer("Sim")
		.Product("Bulk")
		.SerialNumber("1")
		.MsOs20VendorCode(0x42)
		.WebUsb(0x43, "https://example.com/")
		.Configuration(USBConfigurationBuilder()
			.MaxPowerMilliamps(100)
			.Function(USBVendorBuilder()
				.BulkIn(0x81)
				.BulkOut(0x01)
				.WinUsb()));
}>;


// The same two descriptors with one vendor code. The core tells the requests apart by wIndex: 7 for the set, 2 for GET_URL.
using SharedCodeUsb = USBDescriptors<[] {
	return USBDeviceBuilder()
		.VendorId(0x1209)
		.ProductId(0x0001)
		.DeviceRelease(0x0100)
		.Manufacturer("Sim")
		.Product("Bulk")
		.SerialNumber("1")
		.MsOs20VendorCode(0x42)
		.WebUsb(0x42, "https://example.com/")
		.Configuration(USBConfigurationBuilder()
			.MaxPowerMilliamps(100)
			.Function(USBVendorBuilder()
				.BulkIn(0x81)
				.BulkOut(0x01)
				.WinUsb()));
}>;


// WebUSB without MS OS 2.0: the BOS holds only the WebUSB capability, and the device has no MS OS 2.0 set.
using WebUsbOnlyUsb = USBDescriptors<[] {
	return USBDeviceBuilder()
		.VendorId(0x1209)
		.ProductId(0x0001)
		.DeviceRelease(0x0100)
		.Manufacturer("Sim")
		.Product("Bulk")
		.SerialNumber("1")
		.WebUsb(0x43, "http://a")
		.Configuration(USBConfigurationBuilder()
			.MaxPowerMilliamps(100)
			.Function(USBVendorBuilder().BulkIn(0x81).BulkOut(0x01)));
}>;


// WebUSB with the longest URL the descriptor takes: 252 bytes after the https:// prefix, so the descriptor is 255 bytes.
using LongestUrlUsb = USBDescriptors<[] {
	return USBDeviceBuilder()
		.VendorId(0x1209)
		.ProductId(0x0001)
		.DeviceRelease(0x0100)
		.Manufacturer("Sim")
		.Product("Bulk")
		.SerialNumber("1")
		.WebUsb(0x43, "https://aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa")
	.Configuration(USBConfigurationBuilder()
			.MaxPowerMilliamps(100)
			.Function(USBVendorBuilder().BulkIn(0x81).BulkOut(0x01)));
}>;


// A simulated device with BothUsb or SharedCodeUsb, enumerated as a host does. The bulk function is the one the composite tests use.
template<typename TUsb>
struct WebUsbHarness {
	SimUSBDeviceAdapter sim;
	VendorBulkFunction<64, 64, 1> function;
	std::array<USBFunction*, 1> functions;
	USBDevice device;


	WebUsbHarness()
		: function(TUsb::Function(0)),
		  functions{ &function },
		  device(sim, TUsb::Set, functions) {
	}


	// Attaches the device, then resets it and sets the address and configuration, as a host does.
	void Configure() {
		SIM_EXPECT_EQ(device.Start(USBDeviceAdapter::Parameters()), ResultStatus::ok);
		sim.InjectBusReset();
		SIM_EXPECT_EQ(SimHost::Control(sim, SimHost::MakeSetup(0x00, 5, 5, 0x0000, 0)), ResultStatus::ok);
		SIM_EXPECT_EQ(SimHost::Control(sim, SimHost::MakeSetup(0x00, 9, 1, 0x0000, 0)), ResultStatus::ok);
		SIM_EXPECT_EQ(device.GetState(), USBDeviceState::Configured);
	}
};


// A vendor IN request to the device; the reply goes to the buffer, and received is its size.
ResultStatus VendorIn(
	SimUSBDeviceAdapter& sim,
	uint8 request,
	uint16 value,
	uint16 index,
	uint16 length,
	std::span<uint8> reply,
	uint32& received)
{
	return SimHost::Control(sim, SimHost::MakeSetup(0xC0, request, value, index, length), std::span<const uint8>(), reply, received);
}


void CheckDescriptorBytes() {
	// The builders run in constant expressions only, so each result is a constexpr local.
	constexpr std::array<uint8, 24> capability = MakeWebUsbPlatformCapability(0x42, 1);
	constexpr std::array<uint8, 15> httpsUrl = MakeWebUsbUrlDescriptor<"https://example.com/">();
	constexpr std::array<uint8, 4> httpUrl = MakeWebUsbUrlDescriptor<"http://a">();
	constexpr std::array<uint8, 7> noSchemeUrl = MakeWebUsbUrlDescriptor<"vhal">();
	ExpectBytes("capability bytes", capability, CapabilityBytes);
	ExpectBytes("https URL", httpsUrl, HttpsUrlBytes);
	ExpectBytes("http URL", httpUrl, HttpUrlBytes);
	ExpectBytes("URL without scheme", noSchemeUrl, NoSchemeUrlBytes);

	// The longest URL is accepted: 252 bytes after the prefix, bLength 255, scheme 1.
	constexpr std::array<uint8, 255> longestUrl = MakeWebUsbUrlDescriptor<"https://aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa">();
	SIM_EXPECT_EQ(longestUrl[0], 255);
	SIM_EXPECT_EQ(longestUrl[2], 1);
	SIM_EXPECT_EQ(longestUrl[254], 0x61);
}


void CheckBuiltDescriptors() {
	ExpectBytes("BOS with MS OS 2.0 and WebUSB", BothUsb::Bos, BothBosBytes);

	// The BOS announces bcdUSB 0x0210, the lowest version a WebUSB device can say.
	SIM_EXPECT_EQ(BothUsb::Device[2], 0x10);
	SIM_EXPECT_EQ(BothUsb::Device[3], 0x02);

	// Two vendor entries: the set at index 7 with no value check, then GET_URL at index 2 with wValue 1.
	SIM_EXPECT_EQ(BothUsb::Set.vendorRequests.size(), 2);
	SIM_EXPECT_EQ(BothUsb::Set.vendorRequests[0].request, 0x42);
	SIM_EXPECT_EQ(BothUsb::Set.vendorRequests[0].index, 7);
	SIM_EXPECT(!BothUsb::Set.vendorRequests[0].matchValue);
	SIM_EXPECT_EQ(BothUsb::Set.vendorRequests[1].request, 0x43);
	SIM_EXPECT_EQ(BothUsb::Set.vendorRequests[1].index, 2);
	SIM_EXPECT_EQ(BothUsb::Set.vendorRequests[1].value, 1);
	SIM_EXPECT(BothUsb::Set.vendorRequests[1].matchValue);
	ExpectBytes("GET_URL answer", BothUsb::Set.vendorRequests[1].bytes, HttpsUrlBytes);

	// The same vendor code on both: the entries differ only by wIndex.
	SIM_EXPECT_EQ(SharedCodeUsb::Set.vendorRequests.size(), 2);
	SIM_EXPECT_EQ(SharedCodeUsb::Set.vendorRequests[0].request, 0x42);
	SIM_EXPECT_EQ(SharedCodeUsb::Set.vendorRequests[0].index, 7);
	SIM_EXPECT_EQ(SharedCodeUsb::Set.vendorRequests[1].request, 0x42);
	SIM_EXPECT_EQ(SharedCodeUsb::Set.vendorRequests[1].index, 2);

	// WebUSB alone: a BOS of the one capability, bcdUSB 0x0210 by default, and one vendor entry for GET_URL.
	SIM_EXPECT_EQ(WebUsbOnlyUsb::Bos.size(), 29);
	SIM_EXPECT_EQ(WebUsbOnlyUsb::Bos[4], 1);
	SIM_EXPECT_EQ(WebUsbOnlyUsb::Device[2], 0x10);
	SIM_EXPECT(WebUsbOnlyUsb::MsOs20Set.empty());
	SIM_EXPECT_EQ(WebUsbOnlyUsb::Set.vendorRequests.size(), 1);
	SIM_EXPECT_EQ(WebUsbOnlyUsb::Set.vendorRequests[0].request, 0x43);
	ExpectBytes("http GET_URL answer", WebUsbOnlyUsb::Set.vendorRequests[0].bytes, HttpUrlBytes);

	// The builder takes the longest URL: its GET_URL answer is 255 bytes, with scheme 1.
	SIM_EXPECT_EQ(LongestUrlUsb::Set.vendorRequests.size(), 1);
	SIM_EXPECT_EQ(LongestUrlUsb::Set.vendorRequests[0].bytes.size(), 255);
	SIM_EXPECT_EQ(LongestUrlUsb::Set.vendorRequests[0].bytes[2], 1);
}


void CheckDeviceAnswers() {
	WebUsbHarness<BothUsb> harness;
	harness.Configure();

	std::array<uint8, 64> reply{};
	uint32 received = 0;

	// GET_URL with index 1 answers the whole URL descriptor, 15 bytes, and the length the host asked for is no limit.
	SIM_EXPECT_EQ(VendorIn(harness.sim, 0x43, 1, 2, 64, reply, received), ResultStatus::ok);
	SIM_EXPECT_EQ(received, 15);
	ExpectBytes("GET_URL", std::span<const uint8>(reply).first(received), HttpsUrlBytes);

	// A shorter wLength truncates the answer to the bytes asked for.
	SIM_EXPECT_EQ(VendorIn(harness.sim, 0x43, 1, 2, 6, reply, received), ResultStatus::ok);
	SIM_EXPECT_EQ(received, 6);
	ExpectBytes("GET_URL truncated", std::span<const uint8>(reply).first(received), std::span<const uint8>(HttpsUrlBytes).first(6));

	// A URL index that does not exist stalls: index 2, and index 0, which is no URL descriptor.
	SIM_EXPECT_EQ(VendorIn(harness.sim, 0x43, 2, 2, 64, reply, received), ResultStatus::accessError);
	SIM_EXPECT_EQ(VendorIn(harness.sim, 0x43, 0, 2, 64, reply, received), ResultStatus::accessError);

	// A wrong wIndex stalls, both for GET_URL with the MS OS 2.0 index and for the set with the GET_URL index.
	SIM_EXPECT_EQ(VendorIn(harness.sim, 0x43, 1, 3, 64, reply, received), ResultStatus::accessError);
	SIM_EXPECT_EQ(VendorIn(harness.sim, 0x43, 1, 7, 64, reply, received), ResultStatus::accessError);

	// A wrong bRequest stalls.
	SIM_EXPECT_EQ(VendorIn(harness.sim, 0x44, 1, 2, 64, reply, received), ResultStatus::accessError);

	// GET_URL is an IN request. The same bRequest and wIndex sent OUT stall. wLength is 64 so that a core which answers OUT
	// requests with the URL would send data and not stall.
	SIM_EXPECT_EQ(SimHost::Control(harness.sim, SimHost::MakeSetup(0x40, 0x43, 1, 2, 64)), ResultStatus::accessError);

	// The MS OS 2.0 set is answered as before, whatever its wValue: the entry does not check wValue.
	SIM_EXPECT_EQ(VendorIn(harness.sim, 0x42, 0, 7, 64, reply, received), ResultStatus::ok);
	SIM_EXPECT_EQ(received, 30);
	ExpectBytes("MS OS 2.0 set with WebUSB", std::span<const uint8>(reply).first(received), SimDescriptors::MsOs20Set);
	SIM_EXPECT_EQ(VendorIn(harness.sim, 0x42, 5, 7, 64, reply, received), ResultStatus::ok);
	SIM_EXPECT_EQ(received, 30);

	// A wrong wIndex on the MS OS 2.0 code still stalls.
	SIM_EXPECT_EQ(VendorIn(harness.sim, 0x42, 0, 6, 64, reply, received), ResultStatus::accessError);
}


void CheckSharedCodeAnswers() {
	WebUsbHarness<SharedCodeUsb> harness;
	harness.Configure();

	std::array<uint8, 64> reply{};
	uint32 received = 0;

	// One bRequest, two answers: the wIndex picks the descriptor.
	SIM_EXPECT_EQ(VendorIn(harness.sim, 0x42, 1, 2, 64, reply, received), ResultStatus::ok);
	ExpectBytes("shared code GET_URL", std::span<const uint8>(reply).first(received), HttpsUrlBytes);
	SIM_EXPECT_EQ(VendorIn(harness.sim, 0x42, 0, 7, 64, reply, received), ResultStatus::ok);
	SIM_EXPECT_EQ(received, 30);
	SIM_EXPECT_EQ(VendorIn(harness.sim, 0x42, 1, 2, 64, reply, received), ResultStatus::ok);
	SIM_EXPECT_EQ(VendorIn(harness.sim, 0x42, 2, 2, 64, reply, received), ResultStatus::accessError);
}


}


int main() {
	CheckDescriptorBytes();
	CheckBuiltDescriptors();
	CheckDeviceAnswers();
	CheckSharedCodeAnswers();
	return SimTest::Finish("TestWebUsb");
}
