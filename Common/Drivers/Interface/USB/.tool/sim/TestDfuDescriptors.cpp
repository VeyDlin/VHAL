#include <VHAL.h>
#include "DfuTestDevices.h"
#include "SimAssert.h"
#include "SimHost.h"
#include <Drivers/Interface/USB/Class/DFU/DfuFunction.h>
#include <Drivers/Interface/USB/Class/DFU/USBDfuBuilder.h>
#include <Drivers/Interface/USB/Class/DFU/USBDfuProtocol.h>
#include <Drivers/Interface/USB/Descriptor/Builder/USBDescriptors.h>
#include <array>
#include <cstdio>
#include <span>
#include <string_view>
#include <vector>


// The DFU descriptors against bytes written by hand from DFU 1.1 (table 4.1 run-time interface, table 4.2 functional descriptor,
// table 4.4 mode interface) and the USB 2.0 configuration layout. Every field is spelled out, so a builder that writes the wrong
// order or the wrong bit fails here, not on a host.


namespace {


void ExpectBytes(std::span<const uint8> actual, std::span<const uint8> expected, const char* name, int line) {
	bool same = SimHost::SameBytes(actual, expected);
	if (!same) {
		std::fprintf(stderr, "%s:%d: %s differs: %zu bytes against %zu\n", __FILE__, line, name, actual.size(), expected.size());
	}

	SimTest::Expect(same, name, __FILE__, line);
}


#define DFU_EXPECT_BYTES(actual, expected) ExpectBytes((actual), (expected), #actual, __LINE__)


// UTF-16LE of an ASCII text, as the registry property carries it.
std::vector<uint8> Utf16(std::string_view text) {
	std::vector<uint8> bytes;
	for (char value : text) {
		bytes.push_back(static_cast<uint8>(value));
		bytes.push_back(0);
	}

	return bytes;
}


// Little-endian fields of a descriptor, read at offset.
uint32 ReadLe16(std::span<const uint8> bytes, std::size_t offset) {
	return bytes[offset] | (bytes[offset + 1] << 8);
}


uint32 ReadLe32(std::span<const uint8> bytes, std::size_t offset) {
	return ReadLe16(bytes, offset) | (ReadLe16(bytes, offset + 2) << 16);
}


// Mode interface with the full attribute set, a 256 ms detach timeout, a 1024-byte transfer and the name "Flash".
using DescriptorModeUsb = USBDescriptors<+[] {
	return USBDeviceBuilder()
		.VendorId(0x1209)
		.ProductId(0x0002)
		.DeviceRelease(0x0100)
		.Manufacturer("Sim")
		.Product("DFU mode")
		.SerialNumber("1")
		.Configuration(USBConfigurationBuilder()
			.MaxPowerMilliamps(100)
			.Function(USBDfuBuilder()
				.Mode()
				.CanDnload()
				.CanUpload()
				.ManifestationTolerant()
				.WillDetach()
				.DetachTimeoutMs(256)
				.TransferSize(1024)
				.Name("Flash")));
}>;


// A single mode interface with WinUSB and the MS OS 2.0 vendor code: a device-level set and a BOS.
using WinUsbModeUsb = USBDescriptors<+[] {
	return USBDeviceBuilder()
		.VendorId(0x1209)
		.ProductId(0x0002)
		.DeviceRelease(0x0100)
		.Manufacturer("Sim")
		.Product("DFU WinUSB")
		.SerialNumber("1")
		.MsOs20VendorCode(0x20)
		.Configuration(USBConfigurationBuilder()
			.MaxPowerMilliamps(100)
			.Function(USBDfuBuilder()
				.Mode()
				.CanDnload()
				.DetachTimeoutMs(100)
				.TransferSize(64)
				.WinUsb("{A5DCBF10-6530-11D2-901F-00C04FB951ED}")));
}>;


// The run-time device of the state tests: no name, no WillDetach, 255 ms detach timeout, 64-byte transfer.
constexpr std::array<uint8, 27> RuntimeConfigurationBytes = {
	9, 2, 27, 0, 1, 1, 0, 0x80, 50,			// Configuration: wTotalLength 27, one interface, 100 mA
	9, 4, 0, 0, 0, 0xFE, 1, 1, 0,			// Interface 0, alternate 0, no endpoints, class FE, subclass 1, protocol 1 (run time)
	9, 0x21, 0x00, 0xFF, 0x00, 0x40, 0x00, 0x10, 0x01	// Functional: attributes 0, wDetachTimeOut 255, wTransferSize 64, 0x0110
};


// The mode device with the full attribute set: 0x0F, 256 ms (0x0100), 1024 bytes (0x0400), iInterface 4 (after the three device strings).
constexpr std::array<uint8, 27> ModeConfigurationBytes = {
	9, 2, 27, 0, 1, 1, 0, 0x80, 50,			// Configuration: 100 mA
	9, 4, 0, 0, 0, 0xFE, 1, 2, 4,			// Interface 0, alternate 0, class FE, subclass 1, protocol 2 (DFU mode), name string 4
	9, 0x21, 0x0F, 0x00, 0x01, 0x00, 0x04, 0x10, 0x01	// Functional: CanDnload|CanUpload|ManifestationTolerant|WillDetach
};


// The string "Flash" as the device's string 4: bLength 12, type 3, UTF-16LE.
constexpr std::array<uint8, 12> FlashStringBytes = {
	12, 3, 'F', 0, 'l', 0, 'a', 0, 's', 0, 'h', 0
};


void CheckRuntimeConfiguration() {
	using Usb = DfuTest::RuntimeUsb;
	DFU_EXPECT_BYTES(Usb::Configuration, std::span<const uint8>(RuntimeConfigurationBytes));
	SIM_EXPECT_EQ(Usb::Configuration.size(), 27);
	SIM_EXPECT(Usb::Function(0).endpoints.empty());
	SIM_EXPECT_EQ(Usb::Function(0).interfaces.size(), 1);
	SIM_EXPECT_EQ(Usb::Function(0).interfaces[0], 0);

	// The device descriptor: bcdUSB 0x0200 (no BOS), class 0 (per interface), VID 0x1209, PID 0x0003, three strings, one configuration.
	constexpr std::array<uint8, 18> expectedDevice = {
		18, 1, 0x00, 0x02, 0, 0, 0, 64,
		0x09, 0x12, 0x03, 0x00, 0x00, 0x01, 1, 2, 3, 1
	};
	DFU_EXPECT_BYTES(std::span<const uint8>(Usb::Device), std::span<const uint8>(expectedDevice));
}


void CheckRuntimeFunctional() {
	USBDfuFunctional functional = ParseUSBDfuFunctional(DfuTest::RuntimeUsb::Function(0).descriptors);
	SIM_EXPECT(functional.found);
	SIM_EXPECT_EQ(functional.attributes, 0x00);
	SIM_EXPECT_EQ(functional.detachTimeoutMs, 255);
	SIM_EXPECT_EQ(functional.transferSize, 64);
}


void CheckModeConfiguration() {
	using Usb = DescriptorModeUsb;
	DFU_EXPECT_BYTES(Usb::Configuration, std::span<const uint8>(ModeConfigurationBytes));
	DFU_EXPECT_BYTES(Usb::StringTable[4], std::span<const uint8>(FlashStringBytes));
	SIM_EXPECT_EQ(Usb::Configuration[4], 1);

	USBDfuFunctional functional = ParseUSBDfuFunctional(Usb::Function(0).descriptors);
	SIM_EXPECT(functional.found);
	SIM_EXPECT_EQ(functional.attributes, 0x0F);
	SIM_EXPECT_EQ(functional.detachTimeoutMs, 256);
	SIM_EXPECT_EQ(functional.transferSize, 1024);
}


void CheckFunctionalDescriptorMaker() {
	// bLength 9, type 0x21, attributes, wDetachTimeOut 0x1234, wTransferSize 0x0040, bcdDFUVersion 0x0110, little-endian.
	constexpr std::array<uint8, 9> expected = { 9, 0x21, 0x0B, 0x34, 0x12, 0x40, 0x00, 0x10, 0x01 };
	std::array<uint8, 9> made = MakeUSBDfuFunctionalDescriptor(0x0B, 0x1234, 0x0040);
	DFU_EXPECT_BYTES(std::span<const uint8>(made), std::span<const uint8>(expected));
}


void CheckStatusResponse() {
	// bStatus errSTALLEDPKT, bwPollTimeout 0x123456 little-endian, bState dfuDNBUSY (4), iString 0.
	constexpr std::array<uint8, 6> expected = { 0x0F, 0x56, 0x34, 0x12, 0x04, 0x00 };
	std::array<uint8, 6> made = MakeUSBDfuStatusResponse(DfuStatus::errSTALLEDPKT, 0x123456, DfuState::dfuDNBUSY);
	DFU_EXPECT_BYTES(std::span<const uint8>(made), std::span<const uint8>(expected));

	// A poll timeout above the 24-bit field is clamped to 0xFFFFFF.
	constexpr std::array<uint8, 6> clamped = { 0x00, 0xFF, 0xFF, 0xFF, 0x07, 0x00 };
	std::array<uint8, 6> limited = MakeUSBDfuStatusResponse(DfuStatus::OK, 0x01000000, DfuState::dfuMANIFEST);
	DFU_EXPECT_BYTES(std::span<const uint8>(limited), std::span<const uint8>(clamped));
}


void CheckWinUsbDescriptors() {
	using Usb = WinUsbModeUsb;

	// One interface, so the BOS is present (bcdUSB 0x0210) and holds only the MS OS 2.0 capability: 5 + 28 = 33 bytes.
	constexpr std::array<uint8, 5> bosHeader = { 5, 0x0F, 33, 0, 1 };
	SIM_EXPECT_EQ(Usb::Bos.size(), 33);
	DFU_EXPECT_BYTES(std::span<const uint8>(Usb::Bos).first(5), std::span<const uint8>(bosHeader));
	SIM_EXPECT_EQ(Usb::Device[2], 0x10);
	SIM_EXPECT_EQ(Usb::Device[3], 0x02);

	// The MS OS 2.0 set field by field: the set header (wLength 10, descriptor type 0, the Windows version, wTotalLength), the compatible
	// ID (wLength 20, type 3, WINUSB padded to 8 bytes, then the sub-compatible ID of 8 zero bytes), and the DeviceInterfaceGUIDs
	// property (wLength 132, type 4, REG_MULTI_SZ 7, a 42-byte name with its terminator, a 80-byte data field with the GUID and two
	// terminators). A wrong length or type fails here, where a substring match would still find the bytes.
	std::span<const uint8> set(Usb::MsOs20Set);
	std::vector<uint8> guid = Utf16("{A5DCBF10-6530-11D2-901F-00C04FB951ED}");
	constexpr std::array<uint8, 8> compatibleId = { 'W', 'I', 'N', 'U', 'S', 'B', 0, 0 };
	SIM_EXPECT_EQ(set.size(), 162);
	SIM_EXPECT_EQ(ReadLe16(set, 0), 10);
	SIM_EXPECT_EQ(ReadLe16(set, 2), 0);
	SIM_EXPECT_EQ(ReadLe32(set, 4), 0x06030000);
	SIM_EXPECT_EQ(ReadLe16(set, 8), set.size());
	SIM_EXPECT_EQ(ReadLe16(set, 10), 20);
	SIM_EXPECT_EQ(ReadLe16(set, 12), 3);
	DFU_EXPECT_BYTES(set.subspan(14, 8), std::span<const uint8>(compatibleId));
	SIM_EXPECT_EQ(ReadLe16(set, 22), 0);
	SIM_EXPECT_EQ(ReadLe16(set, 30), 132);
	SIM_EXPECT_EQ(ReadLe16(set, 32), 4);
	SIM_EXPECT_EQ(ReadLe16(set, 34), 7);
	SIM_EXPECT_EQ(ReadLe16(set, 36), 42);
	std::vector<uint8> name = Utf16("DeviceInterfaceGUIDs");
	name.push_back(0);
	name.push_back(0);
	DFU_EXPECT_BYTES(set.subspan(38, 42), std::span<const uint8>(name));
	SIM_EXPECT_EQ(ReadLe16(set, 80), 80);
	DFU_EXPECT_BYTES(set.subspan(82, guid.size()), std::span<const uint8>(guid));
	SIM_EXPECT_EQ(ReadLe16(set, 82 + guid.size()), 0);
}


// A mode interface without DetachTimeoutMs(): wDetachTimeOut is not needed for DFU_DETACH on a mode device, so it is 0 and the
// interface builds. The functional descriptor follows the 9-byte configuration and 9-byte interface descriptors.
using ModeNoTimeoutUsb = USBDescriptors<+[] {
	return USBDeviceBuilder()
		.VendorId(0x1209)
		.ProductId(0x0002)
		.DeviceRelease(0x0100)
		.Manufacturer("Sim")
		.Product("DFU mode no timeout")
		.SerialNumber("1")
		.Configuration(USBConfigurationBuilder()
			.MaxPowerMilliamps(100)
			.Function(USBDfuBuilder()
				.Mode()
				.CanDnload()
				.TransferSize(64)));
}>;


void CheckModeWithoutDetachTimeoutBuilds() {
	constexpr std::array<uint8, 9> functional = { 9, 0x21, 0x01, 0x00, 0x00, 0x40, 0x00, 0x10, 0x01 };
	DFU_EXPECT_BYTES(std::span<const uint8>(ModeNoTimeoutUsb::Configuration).subspan(18, 9), std::span<const uint8>(functional));
}


}


int main() {
	CheckRuntimeConfiguration();
	CheckRuntimeFunctional();
	CheckModeConfiguration();
	CheckFunctionalDescriptorMaker();
	CheckStatusResponse();
	CheckWinUsbDescriptors();
	CheckModeWithoutDetachTimeoutBuilds();
	return SimTest::Finish("TestDfuDescriptors");
}
