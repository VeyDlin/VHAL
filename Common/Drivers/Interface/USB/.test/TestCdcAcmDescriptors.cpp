#include <VHAL.h>
#include "CdcTestDevices.h"
#include "SimAssert.h"
#include <Drivers/Interface/USB/Class/CDC/USBCdcProtocol.h>
#include <array>
#include <cstddef>
#include <span>


// The CDC-ACM descriptors, checked byte for byte against arrays written by hand from the USB 2.0 configuration layout (chapter 9),
// the Interface Association Descriptor (ECN of 2003), CDC 1.2 section 5.2 (the functional descriptors and their order) and PSTN 1.2
// section 6.3 (the abstract control model). The header is bcdCDC 0x0110, as the builder writes it. The strings are numbered in order of
// first use: manufacturer 1, product 2, serial 3, and the communication interface's name 4.
namespace {


// Device descriptor. The class is 0xEF/0x02/0x01 because the device has an interface association; bcdUSB is 2.00 since it has no BOS.
constexpr uint8 AutoDeviceBytes[] = {
	0x12, 0x01,				// bLength 18, bDescriptorType 1
	0x00, 0x02,				// bcdUSB 2.00
	0xEF, 0x02, 0x01,		// Miscellaneous, common class, interface association
	0x40,					// bMaxPacketSize0 64
	0x09, 0x12,				// idVendor 0x1209
	0x01, 0x00,				// idProduct 0x0001
	0x00, 0x01,				// bcdDevice 1.00
	0x01, 0x02, 0x03,		// iManufacturer, iProduct, iSerialNumber
	0x01					// bNumConfigurations
};


// Automatic addresses: notification IN 0x81, bulk IN 0x82, bulk OUT 0x03. The configuration is 75 bytes.
constexpr uint8 AutoConfigurationBytes[] = {
	0x09, 0x02, 0x4B, 0x00, 0x02, 0x01, 0x00, 0x80, 0x32,	// configuration: 75 bytes, 2 interfaces, value 1, bus powered, 100 mA
	0x08, 0x0B, 0x00, 0x02, 0x02, 0x02, 0x00, 0x00,			// interface association: first interface 0, 2 interfaces, CDC, ACM, no protocol
	0x09, 0x04, 0x00, 0x00, 0x01, 0x02, 0x02, 0x00, 0x04,	// interface 0: communication, ACM, no protocol, 1 endpoint, string 4
	0x05, 0x24, 0x00, 0x10, 0x01,							// header functional descriptor: bcdCDC 0x0110
	0x05, 0x24, 0x01, 0x00, 0x01,							// call management: no capabilities, data interface 1
	0x04, 0x24, 0x02, 0x02,									// ACM: bmCapabilities 0x02 (line coding, control line state, serial state)
	0x05, 0x24, 0x06, 0x00, 0x01,							// union: master 0, slave 1
	0x07, 0x05, 0x81, 0x03, 0x10, 0x00, 0x10,				// endpoint 0x81: interrupt IN, 16 bytes, 16 ms
	0x09, 0x04, 0x01, 0x00, 0x02, 0x0A, 0x00, 0x00, 0x00,	// interface 1: CDC data, no subclass, no protocol, 2 endpoints, no string
	0x07, 0x05, 0x82, 0x02, 0x40, 0x00, 0x00,				// endpoint 0x82: bulk IN, 64 bytes
	0x07, 0x05, 0x03, 0x02, 0x40, 0x00, 0x00				// endpoint 0x03: bulk OUT, 64 bytes
};


static_assert(sizeof(AutoConfigurationBytes) == 75, "the configuration is 75 bytes");


// Explicit addresses: notification IN 0x83, bulk IN 0x81, bulk OUT 0x02. Only the three endpoint descriptors differ from the above.
constexpr uint8 ExplicitConfigurationBytes[] = {
	0x09, 0x02, 0x4B, 0x00, 0x02, 0x01, 0x00, 0x80, 0x32,
	0x08, 0x0B, 0x00, 0x02, 0x02, 0x02, 0x00, 0x00,
	0x09, 0x04, 0x00, 0x00, 0x01, 0x02, 0x02, 0x00, 0x04,
	0x05, 0x24, 0x00, 0x10, 0x01,
	0x05, 0x24, 0x01, 0x00, 0x01,
	0x04, 0x24, 0x02, 0x02,
	0x05, 0x24, 0x06, 0x00, 0x01,
	0x07, 0x05, 0x83, 0x03, 0x10, 0x00, 0x10,				// endpoint 0x83: interrupt IN, 16 bytes, 16 ms
	0x09, 0x04, 0x01, 0x00, 0x02, 0x0A, 0x00, 0x00, 0x00,
	0x07, 0x05, 0x81, 0x02, 0x40, 0x00, 0x00,				// endpoint 0x81: bulk IN, 64 bytes
	0x07, 0x05, 0x02, 0x02, 0x40, 0x00, 0x00				// endpoint 0x02: bulk OUT, 64 bytes
};


static_assert(sizeof(ExplicitConfigurationBytes) == 75, "the configuration is 75 bytes");


// The same configuration with a 4 ms notification interval: only bInterval of the notification endpoint (its last byte) differs.
constexpr uint8 IntervalConfigurationBytes[] = {
	0x09, 0x02, 0x4B, 0x00, 0x02, 0x01, 0x00, 0x80, 0x32,
	0x08, 0x0B, 0x00, 0x02, 0x02, 0x02, 0x00, 0x00,
	0x09, 0x04, 0x00, 0x00, 0x01, 0x02, 0x02, 0x00, 0x04,
	0x05, 0x24, 0x00, 0x10, 0x01,
	0x05, 0x24, 0x01, 0x00, 0x01,
	0x04, 0x24, 0x02, 0x02,
	0x05, 0x24, 0x06, 0x00, 0x01,
	0x07, 0x05, 0x83, 0x03, 0x10, 0x00, 0x04,
	0x09, 0x04, 0x01, 0x00, 0x02, 0x0A, 0x00, 0x00, 0x00,
	0x07, 0x05, 0x81, 0x02, 0x40, 0x00, 0x00,
	0x07, 0x05, 0x02, 0x02, 0x40, 0x00, 0x00
};


static_assert(sizeof(IntervalConfigurationBytes) == 75, "the configuration is 75 bytes");


// The interface association and the communication interface's name are the same in every case. Each test reads them from its own set.
void CheckAutomaticAddresses() {
	SIM_EXPECT_BYTES(CdcTest::AutoUsb::Device, AutoDeviceBytes);
	SIM_EXPECT_BYTES(CdcTest::AutoUsb::Configuration, AutoConfigurationBytes);

	// The layout the function reads: interfaces 0 and 1, endpoints in the builder's order (notification, bulk IN, bulk OUT).
	USBFunctionLayout layout = CdcTest::AutoUsb::Function(0);
	SIM_EXPECT_EQ(layout.interfaces.size(), 2);
	SIM_EXPECT_EQ(layout.interfaces[0], 0);
	SIM_EXPECT_EQ(layout.interfaces[1], 1);
	SIM_EXPECT_EQ(layout.endpoints.size(), 3);
	SIM_EXPECT_EQ(layout.endpoints[0], 0x81);
	SIM_EXPECT_EQ(layout.endpoints[1], 0x82);
	SIM_EXPECT_EQ(layout.endpoints[2], 0x03);
}


void CheckExplicitAddresses() {
	SIM_EXPECT_BYTES(CdcTest::ExplicitUsb::Configuration, ExplicitConfigurationBytes);

	USBFunctionLayout layout = CdcTest::ExplicitUsb::Function(0);
	SIM_EXPECT_EQ(layout.endpoints.size(), 3);
	SIM_EXPECT_EQ(layout.endpoints[0], 0x83);
	SIM_EXPECT_EQ(layout.endpoints[1], 0x81);
	SIM_EXPECT_EQ(layout.endpoints[2], 0x02);
}


void CheckInterval() {
	SIM_EXPECT_BYTES(CdcTest::IntervalUsb::Configuration, IntervalConfigurationBytes);
}


// The notification endpoint's bInterval (offset 9 + 8 + 9 + 5 + 5 + 4 + 5 + 6 = 51) is 16 by default and the value given otherwise.
// iInterface of the communication interface (offset 9 + 8 + 8 = 25) is the fourth string.
void CheckIntervalAndInterfaceName() {
	SIM_EXPECT_EQ(CdcTest::ExplicitUsb::Configuration[51], 16);
	SIM_EXPECT_EQ(CdcTest::IntervalUsb::Configuration[51], 4);
	SIM_EXPECT_EQ(CdcTest::AutoUsb::Configuration[25], 4);
}


}


int main() {
	CheckAutomaticAddresses();
	CheckExplicitAddresses();
	CheckInterval();
	CheckIntervalAndInterfaceName();
	return SimTest::Finish("TestCdcAcmDescriptors");
}
