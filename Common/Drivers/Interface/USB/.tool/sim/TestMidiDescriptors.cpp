#include <VHAL.h>
#include "HidTestDevices.h"
#include "SimAssert.h"
#include <Drivers/Interface/USB/Class/HID/USBHidBuilder.h>
#include <Drivers/Interface/USB/Class/MIDI/USBMidiBuilder.h>
#include <Drivers/Interface/USB/Descriptor/Builder/USBDescriptors.h>
#include <array>
#include <cstddef>
#include <span>


// The USB-MIDI 1.0 descriptors of the builder, checked byte for byte against arrays written by hand from the MIDI 1.0 and Audio 1.0
// layouts (the Audio Control header and IAD of the Audio class, the MS header, jacks and MS bulk data endpoints of MIDI 1.0 chapter
// 6, and the 9-byte audio-form endpoint). The Linux definitions of the same structures (include/uapi/linux/usb/midi.h) agree with them:
// the MS header is 7 bytes, an IN jack 6, an OUT jack 7 + 2 * pins with iJack last, an MS endpoint 4 + n.
#define MIDI_EXPECT_BYTES(actual, expected) SIM_EXPECT_BYTES((actual), (expected))


namespace {


using OneCableUsb = USBDescriptors<+[] {
	return USBDeviceBuilder()
		.VendorId(0x1209)
		.ProductId(0x0001)
		.DeviceRelease(0x0100)
		.Manufacturer("Sim")
		.Product("MIDI one")
		.SerialNumber("1")
		.Configuration(USBConfigurationBuilder()
			.MaxPowerMilliamps(100)
			.Function(USBMidiBuilder()
				.Cables(1)
				.BulkOut(0x01)
				.BulkIn(0x81)));
}>;


using TwoCableUsb = USBDescriptors<+[] {
	return USBDeviceBuilder()
		.VendorId(0x1209)
		.ProductId(0x0001)
		.DeviceRelease(0x0100)
		.Manufacturer("Sim")
		.Product("MIDI two")
		.SerialNumber("1")
		.Configuration(USBConfigurationBuilder()
			.MaxPowerMilliamps(100)
			.Function(USBMidiBuilder()
				.Cables(2)
				.BulkOut(0x01)
				.BulkIn(0x81)));
}>;


// No endpoint addresses: the configuration builder assigns them, OUT first (0x01), then IN (the next free number, 0x82).
using AutoUsb = USBDescriptors<+[] {
	return USBDeviceBuilder()
		.VendorId(0x1209)
		.ProductId(0x0001)
		.DeviceRelease(0x0100)
		.Manufacturer("Sim")
		.Product("MIDI auto")
		.SerialNumber("1")
		.Configuration(USBConfigurationBuilder()
			.MaxPowerMilliamps(100)
			.Function(USBMidiBuilder()
				.Cables(1)));
}>;


// Two cables with an interface name and a jack name. Both strings are numbered by the configuration, so the test reads them back
// through the string table.
using NamedUsb = USBDescriptors<+[] {
	return USBDeviceBuilder()
		.VendorId(0x1209)
		.ProductId(0x0001)
		.DeviceRelease(0x0100)
		.Manufacturer("Sim")
		.Product("MIDI named")
		.SerialNumber("1")
		.Configuration(USBConfigurationBuilder()
			.MaxPowerMilliamps(100)
			.Function(USBMidiBuilder()
				.Cables(2)
				.BulkOut(0x01)
				.BulkIn(0x81)
				.Name("Port")
				.JackName("Jack")));
}>;


// A keyboard (7-byte interrupt endpoints) and a MIDI function (9-byte bulk endpoints) in one configuration.
using MixedUsb = USBDescriptors<+[] {
	return USBDeviceBuilder()
		.VendorId(0x1209)
		.ProductId(0x0001)
		.DeviceRelease(0x0100)
		.Manufacturer("Sim")
		.Product("MIDI mixed")
		.SerialNumber("1")
		.Configuration(USBConfigurationBuilder()
			.MaxPowerMilliamps(100)
			.Function(USBHidBuilder().BootKeyboard().Report(HidTest::KeyboardReport::Info).In(0x83))
			.Function(USBMidiBuilder()
				.Cables(1)
				.BulkOut(0x02)
				.BulkIn(0x84)));
}>;


// One cable. wTotalLength 109 (0x6D): the configuration header 9, the association 8, the Audio Control interface 9 and its header 9,
// the MIDIStreaming interface 9 and its header 7, four jacks 30, and two endpoints of 9 bytes with a 5-byte MS endpoint each, 14 + 14.
// bMaxPower 0x32 is 100 mA. The jack IDs are 1 to 4, and the IN jacks have iJack 0 because no name is given.
constexpr uint8 OneCableConfigurationBytes[] = {
	0x09, 0x02, 0x6D, 0x00, 0x02, 0x01, 0x00, 0x80, 0x32,	// Configuration: 2 interfaces, value 1, 100 mA
	0x08, 0x0B, 0x00, 0x02, 0x01, 0x00, 0x00, 0x00,		// Association: interfaces 0 and 1, Audio function 0x01/0x00/0x00
	0x09, 0x04, 0x00, 0x00, 0x00, 0x01, 0x01, 0x00, 0x00,	// Audio Control interface 0: no endpoints, subclass 0x01
	0x09, 0x24, 0x01, 0x00, 0x01, 0x09, 0x00, 0x01, 0x01,	// Audio Control header: bcdADC 0x0100, wTotalLength 9, streaming interface 1
	0x09, 0x04, 0x01, 0x00, 0x02, 0x01, 0x03, 0x00, 0x00,	// MIDIStreaming interface 1: two endpoints, subclass 0x03
	0x07, 0x24, 0x01, 0x00, 0x01, 0x25, 0x00,		// MS header: bcdMSC 0x0100, wTotalLength 37 (7 + 30)
	0x06, 0x24, 0x02, 0x01, 0x01, 0x00,			// Cable 0 embedded IN jack, ID 1
	0x06, 0x24, 0x02, 0x02, 0x02, 0x00,			// Cable 0 external IN jack, ID 2
	0x09, 0x24, 0x03, 0x01, 0x03, 0x01, 0x02, 0x01, 0x00,	// Cable 0 embedded OUT jack, ID 3, source external IN jack 2, pin 1
	0x09, 0x24, 0x03, 0x02, 0x04, 0x01, 0x01, 0x01, 0x00,	// Cable 0 external OUT jack, ID 4, source embedded IN jack 1, pin 1
	0x09, 0x05, 0x01, 0x02, 0x40, 0x00, 0x00, 0x00, 0x00,	// Bulk OUT 0x01: 64 bytes, bInterval 0, bRefresh 0, bSynchAddress 0
	0x05, 0x25, 0x01, 0x01, 0x01,				// MS endpoint: one embedded jack, the embedded IN jack 1
	0x09, 0x05, 0x81, 0x02, 0x40, 0x00, 0x00, 0x00, 0x00,	// Bulk IN 0x81: 64 bytes
	0x05, 0x25, 0x01, 0x01, 0x03				// MS endpoint: one embedded jack, the embedded OUT jack 3
};


// Two cables. wTotalLength 141 (0x8D): the one-cable configuration with a second cable's four jacks (30 more bytes) and the
// MS header total 67 (0x43) = 7 + 60. Cable 1 has jack IDs 5 to 8, and each MS endpoint lists its two embedded jacks.
constexpr uint8 TwoCableConfigurationBytes[] = {
	0x09, 0x02, 0x8D, 0x00, 0x02, 0x01, 0x00, 0x80, 0x32,	// Configuration: 2 interfaces, value 1, 100 mA
	0x08, 0x0B, 0x00, 0x02, 0x01, 0x00, 0x00, 0x00,		// Association: interfaces 0 and 1, Audio function 0x01/0x00/0x00
	0x09, 0x04, 0x00, 0x00, 0x00, 0x01, 0x01, 0x00, 0x00,	// Audio Control interface 0: no endpoints, subclass 0x01
	0x09, 0x24, 0x01, 0x00, 0x01, 0x09, 0x00, 0x01, 0x01,	// Audio Control header: bcdADC 0x0100, wTotalLength 9, streaming interface 1
	0x09, 0x04, 0x01, 0x00, 0x02, 0x01, 0x03, 0x00, 0x00,	// MIDIStreaming interface 1: two endpoints, subclass 0x03
	0x07, 0x24, 0x01, 0x00, 0x01, 0x43, 0x00,		// MS header: bcdMSC 0x0100, wTotalLength 67 (7 + 60)
	0x06, 0x24, 0x02, 0x01, 0x01, 0x00,			// Cable 0 embedded IN jack, ID 1
	0x06, 0x24, 0x02, 0x02, 0x02, 0x00,			// Cable 0 external IN jack, ID 2
	0x09, 0x24, 0x03, 0x01, 0x03, 0x01, 0x02, 0x01, 0x00,	// Cable 0 embedded OUT jack, ID 3, source 2, pin 1
	0x09, 0x24, 0x03, 0x02, 0x04, 0x01, 0x01, 0x01, 0x00,	// Cable 0 external OUT jack, ID 4, source 1, pin 1
	0x06, 0x24, 0x02, 0x01, 0x05, 0x00,			// Cable 1 embedded IN jack, ID 5
	0x06, 0x24, 0x02, 0x02, 0x06, 0x00,			// Cable 1 external IN jack, ID 6
	0x09, 0x24, 0x03, 0x01, 0x07, 0x01, 0x06, 0x01, 0x00,	// Cable 1 embedded OUT jack, ID 7, source 6, pin 1
	0x09, 0x24, 0x03, 0x02, 0x08, 0x01, 0x05, 0x01, 0x00,	// Cable 1 external OUT jack, ID 8, source 5, pin 1
	0x09, 0x05, 0x01, 0x02, 0x40, 0x00, 0x00, 0x00, 0x00,	// Bulk OUT 0x01: 64 bytes
	0x06, 0x25, 0x01, 0x02, 0x01, 0x05,			// MS endpoint: two embedded jacks, 1 and 5
	0x09, 0x05, 0x81, 0x02, 0x40, 0x00, 0x00, 0x00, 0x00,	// Bulk IN 0x81: 64 bytes
	0x06, 0x25, 0x01, 0x02, 0x03, 0x07			// MS endpoint: two embedded jacks, 3 and 7
};


// String descriptors of the names in NamedUsb, written by hand: bLength 2 + 2 * characters, type 3, UTF-16LE.
constexpr uint8 PortString[] = {
	0x0A, 0x03, 0x50, 0x00, 0x6F, 0x00, 0x72, 0x00, 0x74, 0x00	// "Port"
};


constexpr uint8 JackString[] = {
	0x0A, 0x03, 0x4A, 0x00, 0x61, 0x00, 0x63, 0x00, 0x6B, 0x00	// "Jack"
};


}


// The device class of a configuration with an interface association is 0xEF/0x02/0x01 (IAD ECN), so the host groups the functions.
void TestDeviceClassWithAssociation() {
	SIM_EXPECT_EQ(OneCableUsb::Device[4], 0xEF);
	SIM_EXPECT_EQ(OneCableUsb::Device[5], 0x02);
	SIM_EXPECT_EQ(OneCableUsb::Device[6], 0x01);
	SIM_EXPECT_EQ(TwoCableUsb::Device[4], 0xEF);
	SIM_EXPECT_EQ(TwoCableUsb::Device[5], 0x02);
	SIM_EXPECT_EQ(TwoCableUsb::Device[6], 0x01);
}


void TestOneCableConfiguration() {
	MIDI_EXPECT_BYTES(std::span<const uint8>(OneCableUsb::Configuration), std::span<const uint8>(OneCableConfigurationBytes));
	SIM_EXPECT_EQ(OneCableUsb::Configuration.size(), sizeof(OneCableConfigurationBytes));
}


void TestTwoCableConfiguration() {
	MIDI_EXPECT_BYTES(std::span<const uint8>(TwoCableUsb::Configuration), std::span<const uint8>(TwoCableConfigurationBytes));
	SIM_EXPECT_EQ(TwoCableUsb::Configuration.size(), sizeof(TwoCableConfigurationBytes));
}


// The layout gives the Audio Control and MIDIStreaming interfaces in that order, and the bulk OUT then IN endpoints, as the
// descriptors do. Without explicit addresses the OUT endpoint takes 0x01 and the IN endpoint takes 0x82, the next number free in
// either direction.
void TestLayoutAndAutomaticAddresses() {
	USBFunctionLayout explicitLayout = TwoCableUsb::Function(0);
	SIM_EXPECT_EQ(explicitLayout.interfaces.size(), 2);
	SIM_EXPECT_EQ(explicitLayout.interfaces[0], 0);
	SIM_EXPECT_EQ(explicitLayout.interfaces[1], 1);
	SIM_EXPECT_EQ(explicitLayout.endpoints.size(), 2);
	SIM_EXPECT_EQ(explicitLayout.endpoints[0], 0x01);
	SIM_EXPECT_EQ(explicitLayout.endpoints[1], 0x81);

	USBFunctionLayout autoLayout = AutoUsb::Function(0);
	SIM_EXPECT_EQ(autoLayout.endpoints.size(), 2);
	SIM_EXPECT_EQ(autoLayout.endpoints[0], 0x01);
	SIM_EXPECT_EQ(autoLayout.endpoints[1], 0x82);
}


// The interface string sits in the MIDIStreaming interface (offset 43) and the jack string in each jack's iJack byte (the last byte
// of each jack). The string table holds the language descriptor at index 0, so string n is entry n. Each index is read back through
// the table and compared with the hand-written text.
void TestStringsAndJackNames() {
	std::span<const uint8> configuration = std::span<const uint8>(NamedUsb::Configuration);
	std::size_t interfaceString = configuration[43];
	SIM_EXPECT(interfaceString != 0);
	SIM_EXPECT(interfaceString < NamedUsb::StringTable.size());
	if (interfaceString >= 1 && interfaceString < NamedUsb::StringTable.size()) {
		MIDI_EXPECT_BYTES(NamedUsb::StringTable[interfaceString], std::span<const uint8>(PortString));
	}

	// iJack of the four jacks of cable 0 (offsets 56, 62, 71, 80) and of cable 1 (86, 92, 101, 110).
	constexpr std::size_t jackOffsets[] = { 56, 62, 71, 80, 86, 92, 101, 110 };
	std::size_t jackString = configuration[56];
	SIM_EXPECT(jackString != 0);
	SIM_EXPECT(jackString != interfaceString);
	for (std::size_t offset : jackOffsets) {
		SIM_EXPECT_EQ(configuration[offset], jackString);
	}

	if (jackString >= 1 && jackString < NamedUsb::StringTable.size()) {
		MIDI_EXPECT_BYTES(NamedUsb::StringTable[jackString], std::span<const uint8>(JackString));
	}
}


// A keyboard with its 7-byte interrupt endpoints and the MIDI function with 9-byte bulk endpoints share the configuration. Interface 0
// is the keyboard, interfaces 1 and 2 the MIDI function, which the layout gives with its own endpoints.
void TestMixedWithHid() {
	SIM_EXPECT_EQ(MixedUsb::Configuration[4], 3);
	USBFunctionLayout keyboardLayout = MixedUsb::Function(0);
	SIM_EXPECT_EQ(keyboardLayout.interfaces.size(), 1);
	SIM_EXPECT_EQ(keyboardLayout.interfaces[0], 0);
	SIM_EXPECT_EQ(keyboardLayout.endpoints[0], 0x83);

	USBFunctionLayout midiLayout = MixedUsb::Function(1);
	SIM_EXPECT_EQ(midiLayout.interfaces.size(), 2);
	SIM_EXPECT_EQ(midiLayout.interfaces[0], 1);
	SIM_EXPECT_EQ(midiLayout.interfaces[1], 2);
	SIM_EXPECT_EQ(midiLayout.endpoints[0], 0x02);
	SIM_EXPECT_EQ(midiLayout.endpoints[1], 0x84);
	SIM_EXPECT_EQ(MixedUsb::Device[4], 0xEF);
}


int main() {
	TestDeviceClassWithAssociation();
	TestOneCableConfiguration();
	TestTwoCableConfiguration();
	TestLayoutAndAutomaticAddresses();
	TestStringsAndJackNames();
	TestMixedWithHid();
	return SimTest::Finish("TestMidiDescriptors");
}
