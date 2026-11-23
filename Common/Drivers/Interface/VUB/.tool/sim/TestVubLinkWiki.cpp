#include <VHAL.h>
#include "CdcTestDevices.h"
#include "HidTestDevices.h"
#include "SimAssert.h"
#include "UartDoubles.h"
#include <Drivers/Interface/USB/Class/CDC/CdcAcmFunction.h>
#include <Drivers/Interface/USB/Class/HID/HidFunction.h>
#include <Drivers/Interface/USB/Class/HID/USBHidReportBuilder.h>
#include <Drivers/Interface/USB/Class/Vendor/VendorBulkFunction.h>
#include <Drivers/Interface/USB/Descriptor/Builder/USBDescriptors.h>
#include <Drivers/Interface/VUB/Builder.h>
#include <Drivers/Interface/VUB/Framing/Cobs.h>
#include <Drivers/Interface/VUB/Link/BulkLink.h>
#include <Drivers/Interface/VUB/Link/CdcLink.h>
#include <Drivers/Interface/VUB/Link/HidLink.h>
#include <Drivers/Interface/VUB/Link/UartLink.h>
#include <Drivers/Interface/VUB/Stack.h>
#include <span>


// The snippets of the link pages of the wiki (Link/UartLink, Link/CdcLink, Link/BulkLink and Link/HidLink), compiled as they are. The
// names AUART and USART1 are the G431's, which the host tree does not have: AUART stands in as the fake port and USART1 as a null handle.
// The USB functions are the real classes, with the descriptor sets of the USB test devices, so each function is a device's own.


// --- Link/UartLink: the G431 console over AUART ---

using AUART = FakeUart;
static FakeUartHandle* const USART1 = nullptr;

AUART uart(USART1);
VUB::UartLink<AUART> uartLink(uart);

void OnConsolePayload(const VUB::Meta&, std::span<const uint8> payload) {
	(void)payload;
}

using Console = VUB::Stack<+[] {
	return VUB::Builder()
		.Framing<VUB::Cobs>()
		.Link<VUB::UartLink<AUART>>()
		.MaxPayload(32)
		.OnReceive<&OnConsolePayload>();
}>;

Console console(uartLink);


// --- Link/CdcLink: the demo's CDC function ---

// The device owns its CDC function: a 512-byte TX ring, a 256-byte RX ring and 128-byte transfers. The test gives it below.
using CdcFunction = CdcAcmFunction<512, 256, 2>;

void OnHostPayload(const VUB::Meta&, std::span<const uint8> payload) {
	(void)payload;
}

using HostCdc = VUB::Stack<+[] {
	return VUB::Builder()
		.Framing<VUB::Cobs>()
		.Link<VUB::CdcLink<CdcFunction>>()
		.MaxPayload(32)
		.OnReceive<&OnHostPayload>();
}>;

CdcTest::CdcFixture cdcFixture;
CdcFunction& usbCdc = cdcFixture.function;

VUB::CdcLink<CdcFunction> cdcLink(usbCdc);
HostCdc cdcStack(cdcLink);

// The main loop calls Process: the stack polls the link, which reads the host's bytes, and then sends what is queued.
void CdcLoop() {
	cdcStack.Process();
}


// --- Link/BulkLink: the demo's vendor bulk function ---

// The device's vendor bulk function: a 512-byte TX ring, a 256-byte RX ring and 128-byte transfers.
using VendorFunction = VendorBulkFunction<512, 256, 2>;

void OnVendorPayload(const VUB::Meta&, std::span<const uint8> payload) {
	(void)payload;
}

using HostVendor = VUB::Stack<+[] {
	return VUB::Builder()
		.Framing<VUB::Cobs>()
		.Link<VUB::BulkLink<VendorFunction>>()
		.MaxPayload(32)
		.OnReceive<&OnVendorPayload>();
}>;

CdcTest::VendorFixtureOf<CdcTest::VendorUsb> vendorFixture;
VendorFunction& usbVendor = vendorFixture.function;

VUB::BulkLink<VendorFunction> bulkLink(usbVendor);
HostVendor vendorStack(bulkLink);

void BulkLoop() {
	vendorStack.Process();
}


// --- Link/HidLink: the demo's HID function, 64-byte reports ---

// The device's HID function: 64-byte input and output reports, and a queue of 8 input reports.
using HidFunctionType = HidFunction<8>;

using Report64 = USBHidReportDescriptor<+[] {
	return USBHidReportBuilder::VendorRaw(64);
}>;

using Vendor64Usb = USBDescriptors<+[] {
	return USBDeviceBuilder()
		.VendorId(0x1209)
		.ProductId(0x0004)
		.DeviceRelease(0x0100)
		.Manufacturer("Sim")
		.Product("Vendor HID 64")
		.SerialNumber("1")
		.Configuration(USBConfigurationBuilder()
			.MaxPowerMilliamps(100)
			.Function(USBHidBuilder().Report(Report64::Info).In(0x81).Out(0x02)));
}>;

void OnRemotePayload(const VUB::Meta&, std::span<const uint8> payload) {
	(void)payload;
}

using Remote = VUB::Stack<+[] {
	return VUB::Builder()
		.Link<VUB::HidLink<HidFunctionType, 64>>()
		.MaxPayload(63)
		.ReceiveQueue(4)
		.OnReceive<&OnRemotePayload>();
}>;

HidTest::HidDevice<Vendor64Usb, Report64, 8> hidDevice;
HidFunctionType& usbHid = hidDevice.function;

VUB::HidLink<HidFunctionType, 64> hidLink(usbHid);
Remote remoteStack(hidLink);

void HidLoop() {
	remoteStack.Process();
}


// The snippets are built and linked. Before the device is configured, each link is down or busy, and the UART port has its
// continuous receive running, which the console's receiver started.
void TestWikiSnippetsCompileAndStartDown() {
	SIM_EXPECT(uart.rxStarts == 1);
	SIM_EXPECT(!cdcStack.LinkUp());
	SIM_EXPECT(!vendorStack.LinkUp());
	CdcLoop();
	BulkLoop();
	HidLoop();
}


int main() {
	TestWikiSnippetsCompileAndStartDown();
	return SimTest::Finish("TestVubLinkWiki");
}
