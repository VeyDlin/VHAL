#include <VHAL.h>
#include "HidTestDevices.h"
#include "SimAssert.h"
#include <Drivers/Interface/USB/Class/HID/USBHidBuilder.h>
#include <Drivers/Interface/USB/Class/HID/USBHidReportBuilder.h>
#include <Drivers/Interface/USB/Descriptor/Builder/USBDescriptors.h>
#include <array>
#include <cstddef>
#include <span>


// The HID class descriptors: the report descriptors of the presets, the HID descriptor, and the configurations of the HID devices. The
// expected bytes are written out by hand from HID 1.11 (Appendix B and section 6.2.2) and the USB 2.0 configuration layout. Every item
// with a value carries at least one data byte, so Logical Minimum 0 is 0x15 0x00 and not 0x14, and a Collection of kind Physical is
// 0xA1 0x00. HID 1.11 allows the zero-size form (bSize 0), but Windows rejects it: the first hardware run failed both HID interfaces on
// those items. Only End Collection takes no data. CheckNoZeroSizeItems() checks that rule on every preset.
namespace {


// HID 1.11 Appendix B.1 with the key array of usages 0 to 255, boot keyboard, 64 bytes. Logical Maximum 255 is 0x26 0xFF 0x00 (the
// two-byte item: 0x25 0xFF would be -1), and Usage Maximum 255 is 0x29 0xFF. Logical Minimum 0 and Usage Minimum 0 take a data byte.
constexpr uint8 KeyboardReportBytes[] = {
	0x05, 0x01,				// Usage Page (Generic Desktop)
	0x09, 0x06,				// Usage (Keyboard)
	0xA1, 0x01,				// Collection (Application)
	0x05, 0x07,				// Usage Page (Key Codes)
	0x19, 0xE0,				// Usage Minimum (224)
	0x29, 0xE7,				// Usage Maximum (231)
	0x15, 0x00,				// Logical Minimum (0)
	0x25, 0x01,				// Logical Maximum (1)
	0x75, 0x01,				// Report Size (1)
	0x95, 0x08,				// Report Count (8)
	0x81, 0x02,				// Input (Data, Variable, Absolute): modifiers
	0x95, 0x01,				// Report Count (1)
	0x75, 0x08,				// Report Size (8)
	0x81, 0x01,				// Input (Constant): reserved byte
	0x95, 0x05,				// Report Count (5)
	0x75, 0x01,				// Report Size (1)
	0x05, 0x08,				// Usage Page (LEDs)
	0x19, 0x01,				// Usage Minimum (1)
	0x29, 0x05,				// Usage Maximum (5)
	0x91, 0x02,				// Output (Data, Variable, Absolute): LEDs
	0x95, 0x01,				// Report Count (1)
	0x75, 0x03,				// Report Size (3)
	0x91, 0x01,				// Output (Constant): padding
	0x95, 0x06,				// Report Count (6)
	0x75, 0x08,				// Report Size (8)
	0x15, 0x00,				// Logical Minimum (0)
	0x26, 0xFF, 0x00,		// Logical Maximum (255)
	0x05, 0x07,				// Usage Page (Key Codes)
	0x19, 0x00,				// Usage Minimum (0)
	0x29, 0xFF,				// Usage Maximum (255)
	0x81, 0x00,				// Input (Data, Array): six key slots
	0xC0					// End Collection
};


// HID 1.11 Appendix B.2 with the wheel appended: buttons, X, Y and wheel, 56 bytes. The first three bytes of a report are the boot report.
constexpr uint8 MouseReportBytes[] = {
	0x05, 0x01,				// Usage Page (Generic Desktop)
	0x09, 0x02,				// Usage (Mouse)
	0xA1, 0x01,				// Collection (Application)
	0x09, 0x01,				// Usage (Pointer)
	0xA1, 0x00,				// Collection (Physical)
	0x05, 0x09,				// Usage Page (Buttons)
	0x19, 0x01,				// Usage Minimum (1)
	0x29, 0x03,				// Usage Maximum (3)
	0x15, 0x00,				// Logical Minimum (0)
	0x25, 0x01,				// Logical Maximum (1)
	0x95, 0x03,				// Report Count (3)
	0x75, 0x01,				// Report Size (1)
	0x81, 0x02,				// Input (Data, Variable, Absolute): buttons
	0x95, 0x01,				// Report Count (1)
	0x75, 0x05,				// Report Size (5)
	0x81, 0x01,				// Input (Constant): padding
	0x05, 0x01,				// Usage Page (Generic Desktop)
	0x09, 0x30,				// Usage (X)
	0x09, 0x31,				// Usage (Y)
	0x15, 0x81,				// Logical Minimum (-127)
	0x25, 0x7F,				// Logical Maximum (127)
	0x75, 0x08,				// Report Size (8)
	0x95, 0x02,				// Report Count (2)
	0x81, 0x06,				// Input (Data, Variable, Relative): X, Y
	0x09, 0x38,				// Usage (Wheel)
	0x95, 0x01,				// Report Count (1)
	0x81, 0x06,				// Input (Data, Variable, Relative): wheel
	0xC0,					// End Collection (Physical)
	0xC0					// End Collection (Application)
};


// Consumer control: one 16-bit usage per report, 23 bytes.
constexpr uint8 ConsumerReportBytes[] = {
	0x05, 0x0C,				// Usage Page (Consumer)
	0x09, 0x01,				// Usage (Consumer Control)
	0xA1, 0x01,				// Collection (Application)
	0x15, 0x00,				// Logical Minimum (0)
	0x26, 0xFF, 0x03,		// Logical Maximum (1023)
	0x19, 0x00,				// Usage Minimum (0)
	0x2A, 0xFF, 0x03,		// Usage Maximum (1023)
	0x75, 0x10,				// Report Size (16)
	0x95, 0x01,				// Report Count (1)
	0x81, 0x00,				// Input (Data, Array)
	0xC0					// End Collection
};


// Vendor raw report of 8 bytes in and 8 bytes out, on usage page 0xFF00, 27 bytes.
constexpr uint8 VendorReportBytes[] = {
	0x06, 0x00, 0xFF,		// Usage Page (0xFF00)
	0x09, 0x01,				// Usage (0x01)
	0xA1, 0x01,				// Collection (Application)
	0x09, 0x01,				// Usage (0x01): input
	0x15, 0x00,				// Logical Minimum (0)
	0x26, 0xFF, 0x00,		// Logical Maximum (255)
	0x75, 0x08,				// Report Size (8)
	0x95, 0x08,				// Report Count (8)
	0x81, 0x02,				// Input (Data, Variable, Absolute)
	0x09, 0x02,				// Usage (0x02): output
	0x95, 0x08,				// Report Count (8)
	0x91, 0x02,				// Output (Data, Variable, Absolute)
	0xC0					// End Collection
};


// The identified report of the test device: input ID 1, feature ID 2, output ID 3, 38 bytes.
constexpr uint8 IdentifiedReportBytes[] = {
	0x06, 0x00, 0xFF,		// Usage Page (0xFF00)
	0x09, 0x01,				// Usage (0x01)
	0xA1, 0x01,				// Collection (Application)
	0x85, 0x01,				// Report ID 1
	0x75, 0x08,				// Report Size (8)
	0x95, 0x02,				// Report Count (2)
	0x09, 0x01,				// Usage (0x01)
	0x81, 0x02,				// Input (Data, Variable, Absolute)
	0x85, 0x02,				// Report ID 2
	0x75, 0x08,				// Report Size (8)
	0x95, 0x03,				// Report Count (3)
	0x09, 0x02,				// Usage (0x02)
	0xB1, 0x02,				// Feature (Data, Variable, Absolute)
	0x85, 0x03,				// Report ID 3
	0x75, 0x08,				// Report Size (8)
	0x95, 0x01,				// Report Count (1)
	0x09, 0x03,				// Usage (0x03)
	0x91, 0x02,				// Output (Data, Variable, Absolute)
	0xC0					// End Collection
};


// Configuration of the boot keyboard: one interface (subclass 1, protocol 1), the HID descriptor, one interrupt IN 0x81 of 8 bytes, 1 ms.
constexpr uint8 KeyboardConfigurationBytes[] = {
	0x09, 0x02, 0x22, 0x00, 0x01, 0x01, 0x00, 0xA0, 0x32,		// configuration, bus powered, remote wakeup, 100 mA, total 34 bytes
	0x09, 0x04, 0x00, 0x00, 0x01, 0x03, 0x01, 0x01, 0x00,		// interface 0, 1 endpoint, class 3, subclass 1, protocol 1
	0x09, 0x21, 0x11, 0x01, 0x00, 0x01, 0x22, 0x40, 0x00,		// HID descriptor, report descriptor 64 bytes
	0x07, 0x05, 0x81, 0x03, 0x08, 0x00, 0x01					// interrupt IN 0x81, 8 bytes, interval 1 ms
};


// Configuration of the boot mouse: subclass 1, protocol 2, report 56 bytes, interrupt IN 0x81 of 4 bytes (the largest report).
constexpr uint8 MouseConfigurationBytes[] = {
	0x09, 0x02, 0x22, 0x00, 0x01, 0x01, 0x00, 0xE0, 0x32,		// configuration, self-powered and remote wakeup, 100 mA, total 34 bytes
	0x09, 0x04, 0x00, 0x00, 0x01, 0x03, 0x01, 0x02, 0x00,		// interface 0, 1 endpoint, class 3, subclass 1, protocol 2
	0x09, 0x21, 0x11, 0x01, 0x00, 0x01, 0x22, 0x38, 0x00,		// HID descriptor, report descriptor 56 bytes
	0x07, 0x05, 0x81, 0x03, 0x04, 0x00, 0x01					// interrupt IN 0x81, 4 bytes, interval 1 ms
};


// Configuration of the vendor raw device: no boot subclass, two endpoints 0x81 and 0x02 of 8 bytes, the HID descriptor of 27 bytes.
constexpr uint8 VendorConfigurationBytes[] = {
	0x09, 0x02, 0x29, 0x00, 0x01, 0x01, 0x00, 0x80, 0x32,		// configuration, 1 interface, 100 mA, total 41 bytes
	0x09, 0x04, 0x00, 0x00, 0x02, 0x03, 0x00, 0x00, 0x00,		// interface 0, 2 endpoints, class 3, no subclass
	0x09, 0x21, 0x11, 0x01, 0x00, 0x01, 0x22, 0x1B, 0x00,		// HID descriptor, report descriptor 27 bytes
	0x07, 0x05, 0x81, 0x03, 0x08, 0x00, 0x01,					// interrupt IN 0x81, 8 bytes, interval 1 ms
	0x07, 0x05, 0x02, 0x03, 0x08, 0x00, 0x01					// interrupt OUT 0x02, 8 bytes, interval 1 ms
};


// The identified device: 41 bytes. The IN packet is the input report with its ID (3 bytes) and the OUT packet the output report (2 bytes).
constexpr uint8 IdentifiedConfigurationBytes[] = {
	0x09, 0x02, 0x29, 0x00, 0x01, 0x01, 0x00, 0x80, 0x32,		// configuration, 1 interface, 100 mA, total 41 bytes
	0x09, 0x04, 0x00, 0x00, 0x02, 0x03, 0x00, 0x00, 0x00,		// interface 0, 2 endpoints, class 3, no subclass
	0x09, 0x21, 0x11, 0x01, 0x00, 0x01, 0x22, 0x26, 0x00,		// HID descriptor, report descriptor 38 bytes
	0x07, 0x05, 0x81, 0x03, 0x03, 0x00, 0x01,					// interrupt IN 0x81, 3 bytes, interval 1 ms
	0x07, 0x05, 0x02, 0x03, 0x02, 0x00, 0x01					// interrupt OUT 0x02, 2 bytes, interval 1 ms
};


// The keyboard with an OUT endpoint: subclass 1, protocol 1, two endpoints.
constexpr uint8 KeyboardOutConfigurationBytes[] = {
	0x09, 0x02, 0x29, 0x00, 0x01, 0x01, 0x00, 0xA0, 0x32,		// configuration, bus powered, remote wakeup, 100 mA, total 41 bytes
	0x09, 0x04, 0x00, 0x00, 0x02, 0x03, 0x01, 0x01, 0x00,		// interface 0, 2 endpoints, class 3, subclass 1, protocol 1
	0x09, 0x21, 0x11, 0x01, 0x00, 0x01, 0x22, 0x40, 0x00,		// HID descriptor, report descriptor 64 bytes
	0x07, 0x05, 0x81, 0x03, 0x08, 0x00, 0x01,					// interrupt IN 0x81, 8 bytes, interval 1 ms
	0x07, 0x05, 0x02, 0x03, 0x01, 0x00, 0x01					// interrupt OUT 0x02, 1 byte (the LED report), interval 1 ms
};


// Packet size and interval given by the application: a mouse on 0x82 with an 8-byte packet and a 10 ms interval.
constexpr uint8 CustomMouseEndpointBytes[] = {
	0x07, 0x05, 0x82, 0x03, 0x08, 0x00, 0x0A
};


// The HID descriptor of a 64-byte report descriptor: bcdHID 0x0111, country 0, one report descriptor of 0x40 bytes.
constexpr uint8 HidDescriptorBytes[] = {
	0x09, 0x21, 0x11, 0x01, 0x00, 0x01, 0x22, 0x40, 0x00
};


// The packet and interval the test sets on the custom mouse.
using CustomMouseUsb = USBDescriptors<+[] {
	return USBDeviceBuilder()
		.VendorId(0x1209)
		.ProductId(0x0001)
		.DeviceRelease(0x0100)
		.Manufacturer("Sim")
		.Product("Custom mouse")
		.SerialNumber("1")
		.Configuration(USBConfigurationBuilder()
			.MaxPowerMilliamps(100)
			.Function(USBHidBuilder().BootMouse().Report(HidTest::MouseReport::Info).PacketSize(8).IntervalMs(10).In(0x82)));
}>;


// Packet sizes of the presets: the keyboard has an 8-byte input report, the mouse 4 bytes, the vendor device 8 bytes in and out.
static_assert(sizeof(KeyboardReportBytes) == 64, "boot keyboard report descriptor is 64 bytes");
static_assert(sizeof(MouseReportBytes) == 56, "boot mouse report descriptor is 56 bytes");
static_assert(sizeof(ConsumerReportBytes) == 23, "consumer control report descriptor is 23 bytes");
static_assert(sizeof(VendorReportBytes) == 27, "vendor report descriptor is 27 bytes");
static_assert(sizeof(IdentifiedReportBytes) == 38, "identified report descriptor is 38 bytes");
static_assert(sizeof(KeyboardConfigurationBytes) == 34, "keyboard configuration is 34 bytes");
static_assert(sizeof(VendorConfigurationBytes) == 41, "vendor configuration is 41 bytes");
static_assert(sizeof(HidDescriptorBytes) == 9, "HID descriptor is 9 bytes");


// Walks the items of a report descriptor (HID 1.11 section 6.2.2.2) and counts the items with bSize 0 that are neither End Collection
// nor Push or Pop. Windows rejects a zero-size item, so the count of a builder's descriptor must be 0. walkEnds is false when the
// items do not add up to the descriptor's length, which means the walk itself went wrong. Long items are not used by the builder.
std::size_t CountZeroSizeItems(std::span<const uint8> descriptor, bool& walkEnds) {
	std::size_t count = 0;
	std::size_t offset = 0;
	while (offset < descriptor.size()) {
		uint8 prefix = descriptor[offset];
		uint8 sizeCode = static_cast<uint8>(prefix & 0x03);
		uint8 type = static_cast<uint8>((prefix >> 2) & 0x03);
		uint8 tag = static_cast<uint8>((prefix >> 4) & 0x0F);
		std::size_t dataBytes = sizeCode == 3 ? 4 : static_cast<std::size_t>(sizeCode);
		if (sizeCode == 0) {
			// End Collection is main tag 0xC. Push and Pop are global tags 0xA and 0xB.
			bool mayBeEmpty = (type == 0 && tag == 0x0C) || (type == 1 && (tag == 0x0A || tag == 0x0B));
			if (!mayBeEmpty) {
				count++;
			}
		}

		offset += 1 + dataBytes;
	}

	walkEnds = offset == descriptor.size();
	return count;
}


// Every preset's descriptor has no zero-size item except End Collection. The walk is checked on the old form too, with a Logical
// Minimum of 0 written as 0x14, so a counter that never finds anything cannot pass.
void CheckNoZeroSizeItems() {
	constexpr uint8 OldEncoding[] = {
		0x05, 0x01,		// Usage Page (Generic Desktop)
		0x09, 0x06,		// Usage (Keyboard)
		0xA1, 0x01,		// Collection (Application)
		0x14,			// Logical Minimum (0), zero-size: the form Windows rejects
		0xA1, 0x00,		// Collection (Physical)
		0x25, 0x01,		// Logical Maximum (1)
		0xC0,			// End Collection (Physical): no data, allowed
		0xC0			// End Collection (Application)
	};

	bool walkEnds = false;
	SIM_EXPECT_EQ(CountZeroSizeItems(OldEncoding, walkEnds), 1);
	SIM_EXPECT(walkEnds);

	bool keyboardEnds = false;
	SIM_EXPECT_EQ(CountZeroSizeItems(HidTest::KeyboardReport::Info.descriptor, keyboardEnds), 0);
	SIM_EXPECT(keyboardEnds);

	bool mouseEnds = false;
	SIM_EXPECT_EQ(CountZeroSizeItems(HidTest::MouseReport::Info.descriptor, mouseEnds), 0);
	SIM_EXPECT(mouseEnds);

	bool consumerEnds = false;
	SIM_EXPECT_EQ(CountZeroSizeItems(HidTest::ConsumerReport::Info.descriptor, consumerEnds), 0);
	SIM_EXPECT(consumerEnds);

	bool vendorEnds = false;
	SIM_EXPECT_EQ(CountZeroSizeItems(HidTest::VendorReport::Info.descriptor, vendorEnds), 0);
	SIM_EXPECT(vendorEnds);

	bool identifiedEnds = false;
	SIM_EXPECT_EQ(CountZeroSizeItems(HidTest::IdentifiedReport::Info.descriptor, identifiedEnds), 0);
	SIM_EXPECT(identifiedEnds);
}


void CheckPresetBytes() {
	SIM_EXPECT_BYTES(HidTest::KeyboardReport::Info.descriptor, KeyboardReportBytes);
	SIM_EXPECT_BYTES(HidTest::MouseReport::Info.descriptor, MouseReportBytes);
	SIM_EXPECT_BYTES(HidTest::ConsumerReport::Info.descriptor, ConsumerReportBytes);
	SIM_EXPECT_BYTES(HidTest::VendorReport::Info.descriptor, VendorReportBytes);
	SIM_EXPECT_BYTES(HidTest::IdentifiedReport::Info.descriptor, IdentifiedReportBytes);
}


// Report lengths per report ID, the rule the function checks SendReport and SET_REPORT lengths against.
void CheckReportLengths() {
	SIM_EXPECT_EQ(HidTest::KeyboardReport::Info.InputLength(0), 8);
	SIM_EXPECT_EQ(HidTest::KeyboardReport::Info.OutputLength(0), 1);
	SIM_EXPECT_EQ(HidTest::KeyboardReport::Info.FeatureLength(0), 0);
	SIM_EXPECT(!HidTest::KeyboardReport::Info.UsesReportIds());

	SIM_EXPECT_EQ(HidTest::MouseReport::Info.InputLength(0), 4);
	SIM_EXPECT_EQ(HidTest::ConsumerReport::Info.InputLength(0), 2);

	SIM_EXPECT_EQ(HidTest::VendorReport::Info.InputLength(0), 8);
	SIM_EXPECT_EQ(HidTest::VendorReport::Info.OutputLength(0), 8);

	// The identified report: the ID byte counts in each length.
	SIM_EXPECT(HidTest::IdentifiedReport::Info.UsesReportIds());
	SIM_EXPECT_EQ(HidTest::IdentifiedReport::Info.InputLength(1), 3);
	SIM_EXPECT_EQ(HidTest::IdentifiedReport::Info.FeatureLength(2), 4);
	SIM_EXPECT_EQ(HidTest::IdentifiedReport::Info.OutputLength(3), 2);
	SIM_EXPECT_EQ(HidTest::IdentifiedReport::Info.InputLength(2), 0);
	SIM_EXPECT_EQ(HidTest::IdentifiedReport::Info.MaxLength(USBHidReportType::Input), 3);
	SIM_EXPECT_EQ(HidTest::IdentifiedReport::Info.reportCount, 3);
}


void CheckHidDescriptor() {
	std::array<uint8, 9> hid = MakeUSBHidDescriptor(64);
	SIM_EXPECT_BYTES(hid, HidDescriptorBytes);
}


void CheckConfigurations() {
	SIM_EXPECT_BYTES(HidTest::KeyboardUsb::Configuration, KeyboardConfigurationBytes);
	SIM_EXPECT_BYTES(HidTest::MouseUsb::Configuration, MouseConfigurationBytes);
	SIM_EXPECT_BYTES(HidTest::VendorUsb::Configuration, VendorConfigurationBytes);
	SIM_EXPECT_BYTES(HidTest::IdentifiedUsb::Configuration, IdentifiedConfigurationBytes);
	SIM_EXPECT_BYTES(HidTest::KeyboardOutUsb::Configuration, KeyboardOutConfigurationBytes);
}


// The device of the boot keyboard reports its class per interface, and no association is needed for one interface.
void CheckDeviceClass() {
	SIM_EXPECT_EQ(HidTest::KeyboardUsb::Device[4], 0x00);
	SIM_EXPECT_EQ(HidTest::KeyboardUsb::Device[5], 0x00);
	SIM_EXPECT_EQ(HidTest::KeyboardUsb::Device[6], 0x00);
	SIM_EXPECT(HidTest::KeyboardUsb::Set.bos.empty());
}


// The layout the function gets: the interface, the endpoints in the order IN, OUT, and the slice of the configuration from the
// interface on (the descriptors after the configuration header).
void CheckLayouts() {
	USBFunctionLayout keyboard = HidTest::KeyboardUsb::Function(0);
	SIM_EXPECT_EQ(keyboard.interfaces.size(), 1);
	SIM_EXPECT_EQ(keyboard.interfaces[0], 0);
	SIM_EXPECT_EQ(keyboard.endpoints.size(), 1);
	SIM_EXPECT_EQ(keyboard.endpoints[0], 0x81);
	SIM_EXPECT_BYTES(keyboard.descriptors, std::span<const uint8>(KeyboardConfigurationBytes).subspan(9));

	USBFunctionLayout vendor = HidTest::VendorUsb::Function(0);
	SIM_EXPECT_EQ(vendor.endpoints.size(), 2);
	SIM_EXPECT_EQ(vendor.endpoints[0], 0x81);
	SIM_EXPECT_EQ(vendor.endpoints[1], 0x02);
}


// PacketSize() and IntervalMs() reach the endpoint descriptor.
void CheckCustomPacketAndInterval() {
	SIM_EXPECT_BYTES(std::span<const uint8>(CustomMouseUsb::Configuration).last(7), CustomMouseEndpointBytes);
}


// Without In() and Out() the configuration builder assigns the addresses: IN 0x81 (the first free number), then OUT 0x02, the
// next number unused in either direction. The result is the configuration with Out(0x02) byte for byte.
using AutoAddressUsb = USBDescriptors<+[] {
	return USBDeviceBuilder()
		.VendorId(0x1209)
		.ProductId(0x0001)
		.DeviceRelease(0x0100)
		.Manufacturer("Sim")
		.Product("Auto")
		.SerialNumber("1")
		.Configuration(USBConfigurationBuilder()
			.MaxPowerMilliamps(100)
			.RemoteWakeup()
			.Function(USBHidBuilder().BootKeyboard().Report(HidTest::KeyboardReport::Info).OutEndpoint()));
}>;


// The same device with a named interface: iInterface is set, and nothing else in the configuration changes length.
using NamedUsb = USBDescriptors<+[] {
	return USBDeviceBuilder()
		.VendorId(0x1209)
		.ProductId(0x0001)
		.DeviceRelease(0x0100)
		.Manufacturer("Sim")
		.Product("Named")
		.SerialNumber("1")
		.Configuration(USBConfigurationBuilder()
			.MaxPowerMilliamps(100)
			.Function(USBHidBuilder().BootKeyboard().Name("Keys").Report(HidTest::KeyboardReport::Info).In(0x81)));
}>;


void CheckAutoAddressesAndName() {
	SIM_EXPECT_BYTES(AutoAddressUsb::Configuration, KeyboardOutConfigurationBytes);
	SIM_EXPECT_EQ(AutoAddressUsb::Function(0).endpoints[0], 0x81);
	SIM_EXPECT_EQ(AutoAddressUsb::Function(0).endpoints[1], 0x02);

	// iInterface is the string index of "Keys". Strings are numbered in order of first use along the chain: Sim (1), Named (2), the
	// serial number (3), then the interface name (4).
	SIM_EXPECT_EQ(NamedUsb::Configuration.size(), sizeof(KeyboardConfigurationBytes));
	SIM_EXPECT(NamedUsb::Configuration[17] != 0);
	SIM_EXPECT_EQ(NamedUsb::Configuration[17], 4);
}


}


int main() {
	CheckNoZeroSizeItems();
	CheckPresetBytes();
	CheckReportLengths();
	CheckHidDescriptor();
	CheckConfigurations();
	CheckDeviceClass();
	CheckLayouts();
	CheckCustomPacketAndInterval();
	CheckAutoAddressesAndName();
	return SimTest::Finish("TestHidDescriptors");
}
