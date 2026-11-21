#include <VHAL.h>
#include "NcmTestDevices.h"
#include "SimAssert.h"
#include "SimHost.h"
#include <Drivers/Interface/USB/Class/CDC/NcmFunction.h>
#include <Drivers/Interface/USB/Class/CDC/USBCdcProtocol.h>
#include <array>
#include <cstddef>
#include <span>


// The CDC-NCM descriptors, checked byte for byte against arrays written by hand from the NCM 1.0 specification (section 5 and table
// 5-1 of the functional descriptors), CDC 1.2 section 5.2 (the header and the union), ECM 1.2 section 5.2 (the Ethernet Networking
// descriptor), USB 2.0 chapter 9 and the interface association. The strings are numbered in order of first use: manufacturer 1,
// product 2, serial 3, the communication interface's name 4, and the run-time MAC string 5.
namespace {


// Device descriptor: class 0xEF/0x02/0x01 because the device has an interface association; bcdUSB 2.00 since it has no BOS.
constexpr uint8 AutoDeviceBytes[] = {
	0x12, 0x01,				// bLength 18, bDescriptorType 1
	0x00, 0x02,				// bcdUSB 2.00
	0xEF, 0x02, 0x01,		// Miscellaneous, common class, interface association
	0x40,					// bMaxPacketSize0 64
	0x09, 0x12,				// idVendor 0x1209
	0x02, 0x00,				// idProduct 0x0002
	0x00, 0x01,				// bcdDevice 1.00
	0x01, 0x02, 0x03,		// iManufacturer, iProduct, iSerialNumber
	0x01					// bNumConfigurations
};


// Automatic addresses: notification IN 0x81, bulk IN 0x82, bulk OUT 0x03. The configuration is 94 bytes.
constexpr uint8 AutoConfigurationBytes[] = {
	0x09, 0x02, 0x5E, 0x00, 0x02, 0x01, 0x00, 0x80, 0x32,	// configuration: 94 bytes, 2 interfaces, value 1, bus powered, 100 mA
	0x08, 0x0B, 0x00, 0x02, 0x02, 0x0D, 0x00, 0x00,			// interface association: first interface 0, 2 interfaces, CDC, NCM, no protocol
	0x09, 0x04, 0x00, 0x00, 0x01, 0x02, 0x0D, 0x00, 0x04,	// interface 0: communication, NCM, no protocol, 1 endpoint, string 4
	0x05, 0x24, 0x00, 0x10, 0x01,							// header functional descriptor: bcdCDC 0x0110
	0x05, 0x24, 0x06, 0x00, 0x01,							// union: master 0, slave 1
	0x0D, 0x24, 0x0F, 0x05, 0x00, 0x00, 0x00, 0x00,			// Ethernet: iMACAddress 5 (the MAC run-time string),
	0xEA, 0x05, 0x00, 0x00, 0x00,							//   statistics 0, wMaxSegmentSize 1514, no filters, no power filters
	0x06, 0x24, 0x1A, 0x00, 0x01, 0x21,						// NCM: bcdNcmVersion 1.00, capabilities 0x21 (packet filter, NTB input size)
	0x07, 0x05, 0x81, 0x03, 0x10, 0x00, 0x10,				// endpoint 0x81: interrupt IN, 16 bytes, 16 ms
	0x09, 0x04, 0x01, 0x00, 0x00, 0x0A, 0x00, 0x01, 0x00,	// interface 1, alternate 0: CDC data, NTB, no endpoints, no string
	0x09, 0x04, 0x01, 0x01, 0x02, 0x0A, 0x00, 0x01, 0x00,	// interface 1, alternate 1: CDC data, NTB, 2 endpoints, no string
	0x07, 0x05, 0x82, 0x02, 0x40, 0x00, 0x00,				// endpoint 0x82: bulk IN, 64 bytes
	0x07, 0x05, 0x03, 0x02, 0x40, 0x00, 0x00				// endpoint 0x03: bulk OUT, 64 bytes
};


static_assert(sizeof(AutoConfigurationBytes) == 94, "the configuration is 94 bytes");


// Explicit addresses: notification IN 0x83, bulk IN 0x81, bulk OUT 0x02. Only the three endpoint descriptors differ from the above.
constexpr uint8 ExplicitConfigurationBytes[] = {
	0x09, 0x02, 0x5E, 0x00, 0x02, 0x01, 0x00, 0x80, 0x32,
	0x08, 0x0B, 0x00, 0x02, 0x02, 0x0D, 0x00, 0x00,
	0x09, 0x04, 0x00, 0x00, 0x01, 0x02, 0x0D, 0x00, 0x04,
	0x05, 0x24, 0x00, 0x10, 0x01,
	0x05, 0x24, 0x06, 0x00, 0x01,
	0x0D, 0x24, 0x0F, 0x05, 0x00, 0x00, 0x00, 0x00, 0xEA, 0x05, 0x00, 0x00, 0x00,
	0x06, 0x24, 0x1A, 0x00, 0x01, 0x21,
	0x07, 0x05, 0x83, 0x03, 0x10, 0x00, 0x10,				// endpoint 0x83: interrupt IN, 16 bytes, 16 ms
	0x09, 0x04, 0x01, 0x00, 0x00, 0x0A, 0x00, 0x01, 0x00,
	0x09, 0x04, 0x01, 0x01, 0x02, 0x0A, 0x00, 0x01, 0x00,
	0x07, 0x05, 0x81, 0x02, 0x40, 0x00, 0x00,				// endpoint 0x81: bulk IN, 64 bytes
	0x07, 0x05, 0x02, 0x02, 0x40, 0x00, 0x00				// endpoint 0x02: bulk OUT, 64 bytes
};


static_assert(sizeof(ExplicitConfigurationBytes) == 94, "the configuration is 94 bytes");


// The same configuration with a 4 ms notification interval: only bInterval of the notification endpoint differs.
constexpr uint8 IntervalConfigurationBytes[] = {
	0x09, 0x02, 0x5E, 0x00, 0x02, 0x01, 0x00, 0x80, 0x32,
	0x08, 0x0B, 0x00, 0x02, 0x02, 0x0D, 0x00, 0x00,
	0x09, 0x04, 0x00, 0x00, 0x01, 0x02, 0x0D, 0x00, 0x04,
	0x05, 0x24, 0x00, 0x10, 0x01,
	0x05, 0x24, 0x06, 0x00, 0x01,
	0x0D, 0x24, 0x0F, 0x05, 0x00, 0x00, 0x00, 0x00, 0xEA, 0x05, 0x00, 0x00, 0x00,
	0x06, 0x24, 0x1A, 0x00, 0x01, 0x21,
	0x07, 0x05, 0x83, 0x03, 0x10, 0x00, 0x04,
	0x09, 0x04, 0x01, 0x00, 0x00, 0x0A, 0x00, 0x01, 0x00,
	0x09, 0x04, 0x01, 0x01, 0x02, 0x0A, 0x00, 0x01, 0x00,
	0x07, 0x05, 0x81, 0x02, 0x40, 0x00, 0x00,
	0x07, 0x05, 0x02, 0x02, 0x40, 0x00, 0x00
};


static_assert(sizeof(IntervalConfigurationBytes) == 94, "the configuration is 94 bytes");


// The MAC string for 02 00 00 AB CD EF: 26 bytes, the digits 0 2 0 0 0 0 A B C D E F, each followed by a zero high byte.
constexpr uint8 MacStringBytes[] = {
	0x1A, 0x03,											// bLength 26, bDescriptorType 3
	0x30, 0x00, 0x32, 0x00, 0x30, 0x00, 0x30, 0x00,		// "0", "2", "0", "0"
	0x30, 0x00, 0x30, 0x00,								// "0", "0"
	0x41, 0x00, 0x42, 0x00, 0x43, 0x00, 0x44, 0x00,		// "A", "B", "C", "D"
	0x45, 0x00, 0x46, 0x00								// "E", "F"
};


static_assert(sizeof(MacStringBytes) == 26, "the MAC string is 26 bytes");


// The Ethernet Networking descriptor as the helper writes it, with iMACAddress 0 (the device fills it in), and the NCM one with
// packet filter and NTB input size advertised.
constexpr uint8 EthernetHelperBytes[] = {
	0x0D, 0x24, 0x0F, 0x00, 0x00, 0x00, 0x00, 0x00, 0xEA, 0x05, 0x00, 0x00, 0x00
};


constexpr uint8 NcmHelperBytes[] = {
	0x06, 0x24, 0x1A, 0x00, 0x01, 0x21
};


// NETWORK_CONNECTION(1) and CONNECTION_SPEED_CHANGE to 12 000 000 bit/s in both directions. 12 000 000 is 0x00B71B00.
constexpr uint8 ConnectedBytes[] = {
	0xA1, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00
};


constexpr uint8 SpeedChangeBytes[] = {
	0xA1, 0x2A, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00,
	0x00, 0x1B, 0xB7, 0x00,
	0x00, 0x1B, 0xB7, 0x00
};


void CheckAutomaticAddresses() {
	SIM_EXPECT_BYTES(NcmTest::AutoUsb::Device, AutoDeviceBytes);
	SIM_EXPECT_BYTES(NcmTest::AutoUsb::Configuration, AutoConfigurationBytes);

	// The layout the function reads: interfaces 0 and 1, endpoints in the builder's order (notification, bulk IN, bulk OUT), and the
	// run-time string index of the MAC address, after the four fixed strings.
	USBFunctionLayout layout = NcmTest::AutoUsb::Function(0);
	SIM_EXPECT_EQ(layout.interfaces.size(), 2);
	SIM_EXPECT_EQ(layout.interfaces[0], 0);
	SIM_EXPECT_EQ(layout.interfaces[1], 1);
	SIM_EXPECT_EQ(layout.endpoints.size(), 3);
	SIM_EXPECT_EQ(layout.endpoints[0], 0x81);
	SIM_EXPECT_EQ(layout.endpoints[1], 0x82);
	SIM_EXPECT_EQ(layout.endpoints[2], 0x03);
	SIM_EXPECT_EQ(layout.runtimeStrings.size(), 1);
	SIM_EXPECT_EQ(layout.runtimeStrings[0], 5);
	SIM_EXPECT_EQ(NcmTest::AutoUsb::Set.runtimeStringIndexes.size(), 1);
}


void CheckExplicitAddresses() {
	SIM_EXPECT_BYTES(NcmTest::ExplicitUsb::Configuration, ExplicitConfigurationBytes);

	USBFunctionLayout layout = NcmTest::ExplicitUsb::Function(0);
	SIM_EXPECT_EQ(layout.endpoints.size(), 3);
	SIM_EXPECT_EQ(layout.endpoints[0], 0x83);
	SIM_EXPECT_EQ(layout.endpoints[1], 0x81);
	SIM_EXPECT_EQ(layout.endpoints[2], 0x02);
}


void CheckInterval() {
	SIM_EXPECT_BYTES(NcmTest::IntervalUsb::Configuration, IntervalConfigurationBytes);
}


// iMACAddress is the Ethernet descriptor's byte 3: offset 9 + 8 + 9 + 5 + 5 + 3 = 39. The communication interface's name is the fourth
// string (offset 25). The notification endpoint starts at offset 55 (9 + 8 + 9 + 5 + 5 + 13 + 6), so its bInterval is at 61.
void CheckIndexesAndInterval() {
	SIM_EXPECT_EQ(NcmTest::AutoUsb::Configuration[39], 5);
	SIM_EXPECT_EQ(NcmTest::AutoUsb::Configuration[25], 4);
	SIM_EXPECT_EQ(NcmTest::ExplicitUsb::Configuration[61], 16);
	SIM_EXPECT_EQ(NcmTest::IntervalUsb::Configuration[61], 4);
}


// The MAC string the application gives, built by the helper from the MAC bytes, is the hand-written string above.
void CheckMacString() {
	SIM_EXPECT_BYTES(NcmTest::MacDescriptor, MacStringBytes);
}


// The class-specific descriptors and the notifications, as the helpers write them.
// The helpers are evaluated at compile time: a run-time call of a descriptor helper's checks would not link on the host.
void CheckHelpers() {
	constexpr std::array<uint8, USBCdcEthernetDescriptorLength> ethernet = MakeUSBCdcEthernetDescriptor();
	constexpr std::array<uint8, USBCdcNcmDescriptorLength> ncm = MakeUSBCdcNcmDescriptor(USBCdcNcmCapPacketFilter | USBCdcNcmCapNtbInputSize);
	constexpr std::array<uint8, USBCdcNetworkConnectionSize> connected = MakeUSBCdcNetworkConnectionNotification(0, true);
	constexpr std::array<uint8, USBCdcConnectionSpeedChangeSize> speed = MakeUSBCdcConnectionSpeedChangeNotification(0, 12000000, 12000000);
	SIM_EXPECT_BYTES(ethernet, EthernetHelperBytes);
	SIM_EXPECT_BYTES(ncm, NcmHelperBytes);
	SIM_EXPECT_BYTES(connected, ConnectedBytes);
	SIM_EXPECT_BYTES(speed, SpeedChangeBytes);
}


}


int main() {
	CheckAutomaticAddresses();
	CheckExplicitAddresses();
	CheckInterval();
	CheckIndexesAndInterval();
	CheckMacString();
	CheckHelpers();
	return SimTest::Finish("TestNcmDescriptors");
}
