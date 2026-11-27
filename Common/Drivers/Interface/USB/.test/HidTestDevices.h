#pragma once
#include <VHAL.h>
#include <Drivers/Interface/USB/Class/HID/HidFunction.h>
#include <Drivers/Interface/USB/Class/HID/HidKeyboard.h>
#include <Drivers/Interface/USB/Class/HID/HidMouse.h>
#include <Drivers/Interface/USB/Class/HID/USBHidBuilder.h>
#include <Drivers/Interface/USB/Class/HID/USBHidReportBuilder.h>
#include <Drivers/Interface/USB/Descriptor/Builder/USBDescriptors.h>
#include <Drivers/Interface/USB/Device/USBDevice.h>
#include "SimAssert.h"
#include "SimHost.h"
#include "SimTestHelpers.h"
#include "SimUSBDeviceAdapter.h"
#include <array>
#include <cstddef>
#include <cstdio>
#include <span>
#include <vector>


// The HID test devices: the report descriptors of the presets and of one identified report, the device descriptor sets that
// carry them, and a fixture that attaches one device the way a host does. The expected bytes are not here; each test writes its own.
namespace HidTest {


using KeyboardReport = USBHidReportDescriptor<+[] { return USBHidReportBuilder::BootKeyboard(); }>;
using MouseReport = USBHidReportDescriptor<+[] { return USBHidReportBuilder::BootMouse(); }>;
using ConsumerReport = USBHidReportDescriptor<+[] { return USBHidReportBuilder::ConsumerControl(); }>;
using VendorReport = USBHidReportDescriptor<+[] { return USBHidReportBuilder::VendorRaw(8); }>;


// Report IDs: input 1 (two data bytes, so 3 bytes with the ID), feature 2 (three data bytes, 4 bytes), output 3 (one data byte, 2 bytes).
using IdentifiedReport = USBHidReportDescriptor<+[] {
	return USBHidReportBuilder()
		.UsagePage(0xFF00)
		.Usage(0x01)
		.Collection(USBHidCollection::Application)
		.ReportId(1)
		.ReportSize(8)
		.ReportCount(2)
		.Usage(0x01)
		.Input(USBHidFlags::Data | USBHidFlags::Variable)
		.ReportId(2)
		.ReportSize(8)
		.ReportCount(3)
		.Usage(0x02)
		.Feature(USBHidFlags::Data | USBHidFlags::Variable)
		.ReportId(3)
		.ReportSize(8)
		.ReportCount(1)
		.Usage(0x03)
		.Output(USBHidFlags::Data | USBHidFlags::Variable)
		.EndCollection();
}>;


// The keyboards advertise remote wakeup, as a keyboard that wakes the host must: the host enables the feature only when the
// configuration says it is supported.
using KeyboardUsb = USBDescriptors<+[] {
	return USBDeviceBuilder()
		.VendorId(0x1209)
		.ProductId(0x0001)
		.DeviceRelease(0x0100)
		.Manufacturer("Sim")
		.Product("Keyboard")
		.SerialNumber("1")
		.Configuration(USBConfigurationBuilder()
			.MaxPowerMilliamps(100)
			.RemoteWakeup()
			.Function(USBHidBuilder().BootKeyboard().Report(KeyboardReport::Info).In(0x81)));
}>;


// The boot keyboard with an interrupt OUT endpoint 0x02, for the LED output report through the endpoint.
using KeyboardOutUsb = USBDescriptors<+[] {
	return USBDeviceBuilder()
		.VendorId(0x1209)
		.ProductId(0x0001)
		.DeviceRelease(0x0100)
		.Manufacturer("Sim")
		.Product("Keyboard out")
		.SerialNumber("1")
		.Configuration(USBConfigurationBuilder()
			.MaxPowerMilliamps(100)
			.RemoteWakeup()
			.Function(USBHidBuilder().BootKeyboard().Report(KeyboardReport::Info).In(0x81).Out(0x02)));
}>;


using MouseUsb = USBDescriptors<+[] {
	return USBDeviceBuilder()
		.VendorId(0x1209)
		.ProductId(0x0001)
		.DeviceRelease(0x0100)
		.Manufacturer("Sim")
		.Product("Mouse")
		.SerialNumber("1")
		.Configuration(USBConfigurationBuilder()
			.MaxPowerMilliamps(100)
			.SelfPowered()
			.RemoteWakeup()
			.Function(USBHidBuilder().BootMouse().Report(MouseReport::Info).In(0x81)));
}>;


// Vendor raw report with its interrupt OUT endpoint, not a boot device.
using VendorUsb = USBDescriptors<+[] {
	return USBDeviceBuilder()
		.VendorId(0x1209)
		.ProductId(0x0001)
		.DeviceRelease(0x0100)
		.Manufacturer("Sim")
		.Product("Vendor HID")
		.SerialNumber("1")
		.Configuration(USBConfigurationBuilder()
			.MaxPowerMilliamps(100)
			.Function(USBHidBuilder().Report(VendorReport::Info).In(0x81).Out(0x02)));
}>;


using IdentifiedUsb = USBDescriptors<+[] {
	return USBDeviceBuilder()
		.VendorId(0x1209)
		.ProductId(0x0001)
		.DeviceRelease(0x0100)
		.Manufacturer("Sim")
		.Product("Identified")
		.SerialNumber("1")
		.Configuration(USBConfigurationBuilder()
			.MaxPowerMilliamps(100)
			.Function(USBHidBuilder().Report(IdentifiedReport::Info).In(0x81).Out(0x02)));
}>;


// One device with one HID function, attached the way a host attaches it. The function queues up to QueueDepth reports.
template<typename TUsb, typename TReport, std::size_t QueueDepth>
struct HidDevice {
	SimUSBDeviceAdapter sim;
	HidFunction<QueueDepth> function;
	std::array<USBFunction*, 1> functions;
	USBDevice device;


	HidDevice()
		: function(TUsb::Function(0), TReport::Info),
		  functions{ &function },
		  device(sim, TUsb::Set, functions) {
	}


	ResultStatus Start() {
		return device.Start(USBDeviceAdapter::Parameters());
	}


	// Resets the bus and configures the device, as a host does after it attaches or resets the device.
	void Enumerate() {
		SimTest::EnumerateDevice(sim, device);
	}


	// Attaches the device and configures it.
	void Configure() {
		SIM_EXPECT_EQ(Start(), ResultStatus::ok);
		Enumerate();
		SIM_EXPECT(device.GetState() == USBDeviceState::Configured);
	}
};


// Reads every report the host can take from an interrupt IN endpoint. Each completion arms the next report, so one call takes all.
inline std::vector<std::vector<uint8>> DrainIn(SimUSBDeviceAdapter& sim, uint8 address) {
	std::vector<std::vector<uint8>> reports;
	for (std::size_t guard = 0; guard < 64; guard++) {
		std::array<uint8, 64> buffer{};
		uint32 received = 0;
		if (sim.HostIn(address, buffer, received) != ResultStatus::ok) {
			break;
		}

		reports.emplace_back(buffer.begin(), buffer.begin() + received);
	}

	return reports;
}


// Expects the reports read from the host to equal the expected list, in order.
inline void ExpectReports(const std::vector<std::vector<uint8>>& actual, const std::vector<std::vector<uint8>>& expected, const char* name, int line) {
	bool same = actual.size() == expected.size();
	for (std::size_t index = 0; same && index < expected.size(); index++) {
		same = SimHost::SameBytes(actual[index], expected[index]);
	}

	if (!same) {
		std::fprintf(stderr, "%s:%d: %s: %zu reports, expected %zu\n", __FILE__, line, name, actual.size(), expected.size());
	}

	SimTest::Expect(same, name, __FILE__, line);
}


}


#define HID_EXPECT_REPORTS(actual, expected) HidTest::ExpectReports((actual), (expected), #actual, __LINE__)
