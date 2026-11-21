#include <VHAL.h>
#include "HidTestDevices.h"
#include "SimAssert.h"
#include "SimHost.h"
#include <Drivers/Interface/USB/Class/HID/HidFunction.h>
#include <array>
#include <chrono>
#include <cstddef>
#include <span>
#include <thread>
#include <vector>


// The HID function through the simulated controller and the host. The host sends the class requests, reads the interrupt IN and
// writes the interrupt OUT the way a host does, and the device core routes every request to the function. The cases are the standard
// and class requests of HID 1.11 chapter 7, the queue, the idle repeat, protocol switching, remote wakeup, halt, and the output reports
// from both paths. The keyboard, mouse, vendor and identified devices are the ones of HidTestDevices.h.


namespace {


using KeyboardDevice = HidTest::HidDevice<HidTest::KeyboardUsb, HidTest::KeyboardReport, 8>;
using KeyboardOutDevice = HidTest::HidDevice<HidTest::KeyboardOutUsb, HidTest::KeyboardReport, 8>;
using SmallKeyboardDevice = HidTest::HidDevice<HidTest::KeyboardUsb, HidTest::KeyboardReport, 2>;
using MouseDevice = HidTest::HidDevice<HidTest::MouseUsb, HidTest::MouseReport, 8>;
using VendorDevice = HidTest::HidDevice<HidTest::VendorUsb, HidTest::VendorReport, 8>;
using IdentifiedDevice = HidTest::HidDevice<HidTest::IdentifiedUsb, HidTest::IdentifiedReport, 8>;


// Report types as they appear in the high byte of wValue of GET_REPORT and SET_REPORT.
constexpr uint8 TypeInput = 1;
constexpr uint8 TypeOutput = 2;
constexpr uint8 TypeFeature = 3;


// What the delegates saw. They run from the host calls, as the interrupt would.
struct Record {
	std::vector<std::vector<uint8>> outputs;
	std::vector<std::vector<uint8>> featureSets;
	uint32 featureGets = 0;
	bool featureSetAccept = true;
};


Record record;


void OnOutput(std::span<const uint8> report) {
	record.outputs.emplace_back(report.begin(), report.end());
}


// The feature report is the report ID, then 0xA1, 0xA2, ... so a test can check the whole answer.
bool OnFeatureGet(uint8 reportId, std::span<uint8> report) {
	record.featureGets++;
	report[0] = reportId;
	for (std::size_t index = 1; index < report.size(); index++) {
		report[index] = static_cast<uint8>(0xA0 + index);
	}

	return true;
}


bool OnFeatureSet(std::span<const uint8> report) {
	record.featureSets.emplace_back(report.begin(), report.end());
	return record.featureSetAccept;
}


// Result of an IN request: its status, and the bytes the device answered with.
struct Answer {
	ResultStatus status = ResultStatus::notReady;
	std::vector<uint8> data;
};


Answer In(SimUSBDeviceAdapter& sim, const USBSetupBytes& setup) {
	std::array<uint8, 256> buffer{};
	uint32 received = 0;
	Answer answer;
	answer.status = SimHost::Control(sim, setup, std::span<const uint8>(), buffer, received);
	answer.data.assign(buffer.begin(), buffer.begin() + received);
	return answer;
}


ResultStatus Out(SimUSBDeviceAdapter& sim, const USBSetupBytes& setup, std::span<const uint8> data) {
	uint32 received = 0;
	return SimHost::Control(sim, setup, data, std::span<uint8>(), received);
}


ResultStatus NoData(SimUSBDeviceAdapter& sim, const USBSetupBytes& setup) {
	return SimHost::Control(sim, setup);
}


USBSetupBytes ReportRequest(bool in, uint8 request, uint8 type, uint8 reportId, uint16 length) {
	uint8 requestType = in ? 0xA1 : 0x21;
	uint16 value = static_cast<uint16>((type << 8) | reportId);
	return SimHost::MakeSetup(requestType, request, value, 0, length);
}


constexpr uint8 GetReport = 0x01;
constexpr uint8 GetIdle = 0x02;
constexpr uint8 GetProtocol = 0x03;
constexpr uint8 SetReport = 0x09;
constexpr uint8 SetIdle = 0x0A;
constexpr uint8 SetProtocol = 0x0B;


std::vector<uint8> Bytes(std::initializer_list<uint8> values) {
	return std::vector<uint8>(values);
}


// A keyboard report: modifiers, reserved, six keys.
constexpr std::array<uint8, 8> ReportA = { 0x02, 0x00, 0x04, 0x05, 0x00, 0x00, 0x00, 0x00 };
constexpr std::array<uint8, 8> ReportB = { 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00 };


void CheckNotConfigured() {
	KeyboardDevice device;
	SIM_EXPECT_EQ(device.Start(), ResultStatus::ok);

	// Before SET_CONFIGURATION a report is refused: the host cannot read it yet.
	ResultStatus status = device.function.SendReport(ReportA);
	SIM_EXPECT_EQ(status, ResultStatus::notReady);

	// The length is checked first, so a wrong length is invalidParameter whatever the state.
	status = device.function.SendReport(std::span<const uint8>(ReportA).first(7));
	SIM_EXPECT_EQ(status, ResultStatus::invalidParameter);
}


// The standard requests the function answers: GET_DESCRIPTOR of the HID and of the report descriptor, and what it refuses.
void CheckDescriptorRequests() {
	KeyboardDevice device;
	device.Configure();
	SimUSBDeviceAdapter& sim = device.sim;

	Answer hid = In(sim, SimHost::MakeSetup(0x81, 6, 0x2100, 0, 9));
	SIM_EXPECT_EQ(hid.status, ResultStatus::ok);
	SIM_EXPECT_EQ(hid.data.size(), 9);
	SIM_EXPECT_EQ(hid.data[1], 0x21);
	SIM_EXPECT_EQ(hid.data[7], 0x40);
	SIM_EXPECT_EQ(hid.data[8], 0x00);

	// The report descriptor, read in full and in part: a host that asks for less gets the first bytes.
	Answer report = In(sim, SimHost::MakeSetup(0x81, 6, 0x2200, 0, 64));
	SIM_EXPECT_EQ(report.status, ResultStatus::ok);
	SIM_EXPECT_BYTES(report.data, HidTest::KeyboardReport::Info.descriptor);
	SIM_EXPECT_EQ(report.data.size(), 64);

	Answer part = In(sim, SimHost::MakeSetup(0x81, 6, 0x2200, 0, 16));
	SIM_EXPECT_EQ(part.data.size(), 16);
	SIM_EXPECT_BYTES(part.data, std::span<const uint8>(HidTest::KeyboardReport::Info.descriptor).first(16));

	// Another descriptor type of the interface is not answered.
	Answer other = In(sim, SimHost::MakeSetup(0x81, 6, 0x2300, 0, 16));
	SIM_EXPECT_EQ(other.status, ResultStatus::accessError);
}


// GET_REPORT of the input before anything was sent gives zeros, and after SendReport it gives the last report.
void CheckGetReportInput() {
	KeyboardDevice device;
	device.Configure();
	SimUSBDeviceAdapter& sim = device.sim;

	Answer before = In(sim, ReportRequest(true, GetReport, TypeInput, 0, 8));
	SIM_EXPECT_EQ(before.status, ResultStatus::ok);
	SIM_EXPECT_EQ(before.data.size(), 8);
	SIM_EXPECT_EQ(before.data[2], 0);

	SIM_EXPECT_EQ(device.function.SendReport(ReportA), ResultStatus::ok);
	device.device.Process();
	std::vector<std::vector<uint8>> sent = HidTest::DrainIn(sim, 0x81);
	SIM_EXPECT_EQ(sent.size(), 1);

	Answer after = In(sim, ReportRequest(true, GetReport, TypeInput, 0, 8));
	SIM_EXPECT_BYTES(after.data, ReportA);

	// A feature report that the descriptor does not have is refused.
	Answer feature = In(sim, ReportRequest(true, GetReport, TypeFeature, 0, 8));
	SIM_EXPECT_EQ(feature.status, ResultStatus::accessError);
}


// SET_REPORT of an output report reaches the delegate; GET_REPORT of output gives the last one. A wrong length stalls.
void CheckSetReportOutput() {
	KeyboardDevice device;
	record = Record();
	SIM_EXPECT_EQ(device.function.SetOutputDelegate(HidFunction<8>::OutputDelegate::Bind<&OnOutput>()), ResultStatus::ok);
	device.Configure();
	SimUSBDeviceAdapter& sim = device.sim;

	Answer none = In(sim, ReportRequest(true, GetReport, TypeOutput, 0, 1));
	SIM_EXPECT_EQ(none.data.size(), 1);
	SIM_EXPECT_EQ(none.data[0], 0);

	std::array<uint8, 1> leds = { 0x03 };
	SIM_EXPECT_EQ(Out(sim, ReportRequest(false, SetReport, TypeOutput, 0, 1), leds), ResultStatus::ok);
	SIM_EXPECT_EQ(record.outputs.size(), 1);
	SIM_EXPECT_BYTES(record.outputs[0], leds);

	Answer last = In(sim, ReportRequest(true, GetReport, TypeOutput, 0, 1));
	SIM_EXPECT_BYTES(last.data, leds);

	// Two bytes against a one-byte output report: the request is refused, and the delegate is not called.
	std::array<uint8, 2> wrong = { 0x01, 0x02 };
	SIM_EXPECT_EQ(Out(sim, ReportRequest(false, SetReport, TypeOutput, 0, 2), wrong), ResultStatus::accessError);
	SIM_EXPECT_EQ(record.outputs.size(), 1);
}


// Feature reports go through the delegates. Without a delegate the request is refused.
void CheckFeatureReports() {
	IdentifiedDevice device;
	record = Record();
	device.Configure();
	SIM_EXPECT_EQ(device.function.SetFeatureGetDelegate(HidFunction<8>::FeatureGetDelegate::Bind<&OnFeatureGet>()), ResultStatus::busy);

	IdentifiedDevice unset;
	SIM_EXPECT_EQ(unset.function.SetFeatureGetDelegate(HidFunction<8>::FeatureGetDelegate::Bind<&OnFeatureGet>()), ResultStatus::ok);
	SIM_EXPECT_EQ(unset.function.SetFeatureSetDelegate(HidFunction<8>::FeatureSetDelegate::Bind<&OnFeatureSet>()), ResultStatus::ok);
	unset.Configure();
	SimUSBDeviceAdapter& unsetSim = unset.sim;

	// The get delegate fills the report, ID byte included: 4 bytes for feature 2.
	Answer feature = In(unsetSim, ReportRequest(true, GetReport, TypeFeature, 2, 4));
	SIM_EXPECT_EQ(feature.status, ResultStatus::ok);
	constexpr std::array<uint8, 4> FeatureReport = { 0x02, 0xA1, 0xA2, 0xA3 };
	SIM_EXPECT_BYTES(feature.data, FeatureReport);
	SIM_EXPECT_EQ(record.featureGets, 1);

	std::array<uint8, 4> set = { 0x02, 0x07, 0x08, 0x09 };
	SIM_EXPECT_EQ(Out(unsetSim, ReportRequest(false, SetReport, TypeFeature, 2, 4), set), ResultStatus::ok);
	SIM_EXPECT_EQ(record.featureSets.size(), 1);
	SIM_EXPECT_BYTES(record.featureSets[0], set);

	// A delegate that refuses stalls the request.
	record.featureSetAccept = false;
	SIM_EXPECT_EQ(Out(unsetSim, ReportRequest(false, SetReport, TypeFeature, 2, 4), set), ResultStatus::accessError);
	record.featureSetAccept = true;

	// A device without the feature delegates refuses both requests.
	IdentifiedDevice bare;
	bare.Configure();
	SimUSBDeviceAdapter& bareSim = bare.sim;
	SIM_EXPECT_EQ(In(bareSim, ReportRequest(true, GetReport, TypeFeature, 2, 4)).status, ResultStatus::accessError);
	SIM_EXPECT_EQ(Out(bareSim, ReportRequest(false, SetReport, TypeFeature, 2, 4), set), ResultStatus::accessError);
}


// The identified report: input 1 is 3 bytes with its ID, and SendReport checks the ID and the length.
void CheckReportIdsAndLengths() {
	IdentifiedDevice device;
	record = Record();
	SIM_EXPECT_EQ(device.function.SetOutputDelegate(HidFunction<8>::OutputDelegate::Bind<&OnOutput>()), ResultStatus::ok);
	device.Configure();
	SimUSBDeviceAdapter& sim = device.sim;

	std::array<uint8, 3> good = { 0x01, 0xAA, 0xBB };
	SIM_EXPECT_EQ(device.function.SendReport(good), ResultStatus::ok);

	std::array<uint8, 2> shortReport = { 0x01, 0xAA };
	SIM_EXPECT_EQ(device.function.SendReport(shortReport), ResultStatus::invalidParameter);

	std::array<uint8, 4> featureId = { 0x02, 0x01, 0x02, 0x03 };
	SIM_EXPECT_EQ(device.function.SendReport(featureId), ResultStatus::invalidParameter);

	std::array<uint8, 3> zeroId = { 0x00, 0x01, 0x02 };
	SIM_EXPECT_EQ(device.function.SendReport(zeroId), ResultStatus::invalidParameter);

	std::array<uint8, 3> unknownId = { 0x09, 0x01, 0x02 };
	SIM_EXPECT_EQ(device.function.SendReport(unknownId), ResultStatus::invalidParameter);

	SIM_EXPECT_EQ(device.function.SendReport(std::span<const uint8>()), ResultStatus::invalidParameter);

	device.device.Process();
	std::vector<std::vector<uint8>> sent = HidTest::DrainIn(sim, 0x81);
	HidTest::ExpectReports(sent, { Bytes({ 0x01, 0xAA, 0xBB }) }, "identified report sent", __LINE__);

	// The output report of ID 3 is 2 bytes, delivered from SET_REPORT.
	std::array<uint8, 2> output = { 0x03, 0x55 };
	SIM_EXPECT_EQ(Out(sim, ReportRequest(false, SetReport, TypeOutput, 3, 2), output), ResultStatus::ok);
	SIM_EXPECT_EQ(record.outputs.size(), 1);
	SIM_EXPECT_BYTES(record.outputs[0], output);

	Answer back = In(sim, ReportRequest(true, GetReport, TypeOutput, 3, 2));
	SIM_EXPECT_BYTES(back.data, output);

	std::array<uint8, 3> wrongLength = { 0x03, 0x55, 0x66 };
	SIM_EXPECT_EQ(Out(sim, ReportRequest(false, SetReport, TypeOutput, 3, 3), wrongLength), ResultStatus::accessError);
	SIM_EXPECT_EQ(record.outputs.size(), 1);
}


// Idle: GET_IDLE and SET_IDLE per report ID, and a request for an ID the device does not have is refused.
void CheckIdleRequests() {
	KeyboardDevice device;
	device.Configure();
	SimUSBDeviceAdapter& sim = device.sim;

	Answer zero = In(sim, SimHost::MakeSetup(0xA1, GetIdle, 0, 0, 1));
	SIM_EXPECT_EQ(zero.status, ResultStatus::ok);
	SIM_EXPECT_EQ(zero.data[0], 0);

	SIM_EXPECT_EQ(NoData(sim, SimHost::MakeSetup(0x21, SetIdle, static_cast<uint16>(4 << 8), 0, 0)), ResultStatus::ok);
	Answer four = In(sim, SimHost::MakeSetup(0xA1, GetIdle, 0, 0, 1));
	SIM_EXPECT_EQ(four.data[0], 4);

	// SET_IDLE with ID 0 applies to every report; the keyboard's only report is ID 0.
	SIM_EXPECT_EQ(NoData(sim, SimHost::MakeSetup(0x21, SetIdle, 0, 0, 0)), ResultStatus::ok);
	Answer cleared = In(sim, SimHost::MakeSetup(0xA1, GetIdle, 0, 0, 1));
	SIM_EXPECT_EQ(cleared.data[0], 0);

	// An ID the device does not have is refused.
	SIM_EXPECT_EQ(NoData(sim, SimHost::MakeSetup(0x21, SetIdle, static_cast<uint16>(4 << 8) | 5, 0, 0)), ResultStatus::accessError);
	SIM_EXPECT_EQ(In(sim, SimHost::MakeSetup(0xA1, GetIdle, 5, 0, 1)).status, ResultStatus::accessError);

	// The identified device keeps one idle period per ID: 1 and 3 are separate.
	IdentifiedDevice identified;
	identified.Configure();
	SimUSBDeviceAdapter& identifiedSim = identified.sim;
	SIM_EXPECT_EQ(NoData(identifiedSim, SimHost::MakeSetup(0x21, SetIdle, static_cast<uint16>(5 << 8) | 1, 0, 0)), ResultStatus::ok);
	SIM_EXPECT_EQ(In(identifiedSim, SimHost::MakeSetup(0xA1, GetIdle, 1, 0, 1)).data[0], 5);
	SIM_EXPECT_EQ(In(identifiedSim, SimHost::MakeSetup(0xA1, GetIdle, 3, 0, 1)).data[0], 0);
}


// Protocol: the keyboard starts in report protocol, switches to boot and back. A vendor interface has no boot subclass and stalls.
void CheckProtocolRequests() {
	KeyboardDevice keyboard;
	keyboard.Configure();
	SimUSBDeviceAdapter& keyboardSim = keyboard.sim;

	SIM_EXPECT_EQ(In(keyboardSim, SimHost::MakeSetup(0xA1, GetProtocol, 0, 0, 1)).data[0], 1);
	SIM_EXPECT_EQ(NoData(keyboardSim, SimHost::MakeSetup(0x21, SetProtocol, 0, 0, 0)), ResultStatus::ok);
	SIM_EXPECT_EQ(In(keyboardSim, SimHost::MakeSetup(0xA1, GetProtocol, 0, 0, 1)).data[0], 0);

	// Only 0 (boot) and 1 (report) exist.
	SIM_EXPECT_EQ(NoData(keyboardSim, SimHost::MakeSetup(0x21, SetProtocol, 2, 0, 0)), ResultStatus::accessError);
	SIM_EXPECT_EQ(In(keyboardSim, SimHost::MakeSetup(0xA1, GetProtocol, 0, 0, 1)).data[0], 0);

	VendorDevice vendor;
	vendor.Configure();
	SimUSBDeviceAdapter& vendorSim = vendor.sim;
	SIM_EXPECT_EQ(NoData(vendorSim, SimHost::MakeSetup(0x21, SetProtocol, 0, 0, 0)), ResultStatus::accessError);
	SIM_EXPECT_EQ(In(vendorSim, SimHost::MakeSetup(0xA1, GetProtocol, 0, 0, 1)).status, ResultStatus::accessError);
}


// In boot protocol the mouse sends its first three bytes, the boot report. A bus reset returns the device to report protocol.
void CheckBootProtocolReports() {
	MouseDevice mouse;
	mouse.Configure();
	SimUSBDeviceAdapter& sim = mouse.sim;
	std::array<uint8, 4> report = { 0x01, 0x05, 0xFE, 0x02 };

	SIM_EXPECT_EQ(NoData(sim, SimHost::MakeSetup(0x21, SetProtocol, 0, 0, 0)), ResultStatus::ok);
	SIM_EXPECT_EQ(mouse.function.SendReport(report), ResultStatus::ok);
	mouse.device.Process();
	std::vector<std::vector<uint8>> boot = HidTest::DrainIn(sim, 0x81);
	HidTest::ExpectReports(boot, { Bytes({ 0x01, 0x05, 0xFE }) }, "boot protocol report", __LINE__);

	SIM_EXPECT_EQ(NoData(sim, SimHost::MakeSetup(0x21, SetProtocol, 1, 0, 0)), ResultStatus::ok);
	SIM_EXPECT_EQ(mouse.function.SendReport(report), ResultStatus::ok);
	mouse.device.Process();
	std::vector<std::vector<uint8>> full = HidTest::DrainIn(sim, 0x81);
	HidTest::ExpectReports(full, { Bytes({ 0x01, 0x05, 0xFE, 0x02 }) }, "report protocol report", __LINE__);

	SIM_EXPECT_EQ(NoData(sim, SimHost::MakeSetup(0x21, SetProtocol, 0, 0, 0)), ResultStatus::ok);
	mouse.Enumerate();
	SIM_EXPECT_EQ(In(sim, SimHost::MakeSetup(0xA1, GetProtocol, 0, 0, 1)).data[0], 1);
}


// The queue holds QueueDepth reports. A report in flight keeps its place until the host has taken it, so a full queue is busy.
void CheckQueueFull() {
	SmallKeyboardDevice device;
	device.Configure();
	SimUSBDeviceAdapter& sim = device.sim;

	SIM_EXPECT_EQ(device.function.FreeSlots(), 2);
	SIM_EXPECT_EQ(device.function.SendReport(ReportA), ResultStatus::ok);
	SIM_EXPECT_EQ(device.function.SendReport(ReportB), ResultStatus::ok);
	SIM_EXPECT_EQ(device.function.SendReport(ReportA), ResultStatus::busy);
	SIM_EXPECT_EQ(device.function.FreeSlots(), 0);

	device.device.Process();
	std::vector<std::vector<uint8>> sent = HidTest::DrainIn(sim, 0x81);
	HidTest::ExpectReports(sent, { Bytes({ 0x02, 0x00, 0x04, 0x05, 0x00, 0x00, 0x00, 0x00 }), Bytes({ 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00 }) },
		"queued reports in order", __LINE__);
	SIM_EXPECT_EQ(device.function.FreeSlots(), 2);
	SIM_EXPECT_EQ(device.function.SendReport(ReportB), ResultStatus::ok);
}


// Idle repeat: after a period the last input report is queued again, once per period. A zero period sends nothing more.
void CheckIdleRepeat() {
	KeyboardDevice device;
	device.Configure();
	SimUSBDeviceAdapter& sim = device.sim;

	SIM_EXPECT_EQ(device.function.SendReport(ReportA), ResultStatus::ok);
	device.device.Process();
	SIM_EXPECT_EQ(HidTest::DrainIn(sim, 0x81).size(), 1);

	// 4 ms units: a period of 1 repeats every 4 ms. Each wait is 3 times that, so each Process() sees one period passed.
	SIM_EXPECT_EQ(NoData(sim, SimHost::MakeSetup(0x21, SetIdle, static_cast<uint16>(1 << 8), 0, 0)), ResultStatus::ok);
	std::size_t repeats = 0;
	for (std::size_t round = 0; round < 4; round++) {
		std::this_thread::sleep_for(std::chrono::milliseconds(12));
		device.device.Process();
		std::vector<std::vector<uint8>> again = HidTest::DrainIn(sim, 0x81);
		for (const std::vector<uint8>& report : again) {
			SIM_EXPECT_BYTES(report, ReportA);
		}

		repeats += again.size();
	}

	SIM_EXPECT_EQ(repeats, 4);

	SIM_EXPECT_EQ(NoData(sim, SimHost::MakeSetup(0x21, SetIdle, 0, 0, 0)), ResultStatus::ok);
	std::this_thread::sleep_for(std::chrono::milliseconds(12));
	device.device.Process();
	SIM_EXPECT_EQ(HidTest::DrainIn(sim, 0x81).size(), 0);
}


// Remote wakeup: the device asks for resume when a report waits while suspended, if the host enabled remote wakeup. Without it the
// request is refused and the report waits for the host's own resume.
void CheckRemoteWakeup() {
	KeyboardDevice enabled;
	enabled.Configure();
	SimUSBDeviceAdapter& sim = enabled.sim;
	SIM_EXPECT_EQ(NoData(sim, SimHost::MakeSetup(0x00, 3, 1, 0, 0)), ResultStatus::ok);
	SIM_EXPECT(enabled.device.IsRemoteWakeupEnabled());

	SIM_EXPECT_EQ(enabled.function.SendReport(ReportA), ResultStatus::ok);
	sim.InjectSuspend();
	enabled.device.Process();
	SIM_EXPECT_EQ(HidTest::DrainIn(sim, 0x81).size(), 0);
	SIM_EXPECT_EQ(sim.FinishResume(), ResultStatus::ok);
	SIM_EXPECT(enabled.device.GetState() == USBDeviceState::Configured);

	enabled.device.Process();
	std::vector<std::vector<uint8>> sent = HidTest::DrainIn(sim, 0x81);
	HidTest::ExpectReports(sent, { Bytes({ 0x02, 0x00, 0x04, 0x05, 0x00, 0x00, 0x00, 0x00 }) }, "report after resume", __LINE__);

	KeyboardDevice disabled;
	disabled.Configure();
	SimUSBDeviceAdapter& disabledSim = disabled.sim;
	SIM_EXPECT_EQ(disabled.function.SendReport(ReportA), ResultStatus::ok);
	disabledSim.InjectSuspend();
	disabled.device.Process();
	SIM_EXPECT_EQ(disabledSim.FinishResume(), ResultStatus::notReady);
	SIM_EXPECT(disabled.device.GetState() == USBDeviceState::Suspended);
}


// A stalled interrupt endpoint sends nothing, and the queued report goes out once the host clears the halt.
void CheckHalt() {
	KeyboardDevice device;
	device.Configure();
	SimUSBDeviceAdapter& sim = device.sim;

	SIM_EXPECT_EQ(device.function.SendReport(ReportA), ResultStatus::ok);
	SIM_EXPECT_EQ(NoData(sim, SimHost::MakeSetup(0x02, 3, 0, 0x81, 0)), ResultStatus::ok);
	device.device.Process();
	SIM_EXPECT_EQ(HidTest::DrainIn(sim, 0x81).size(), 0);

	SIM_EXPECT_EQ(NoData(sim, SimHost::MakeSetup(0x02, 1, 0, 0x81, 0)), ResultStatus::ok);
	std::vector<std::vector<uint8>> sent = HidTest::DrainIn(sim, 0x81);
	HidTest::ExpectReports(sent, { Bytes({ 0x02, 0x00, 0x04, 0x05, 0x00, 0x00, 0x00, 0x00 }) }, "report after clear", __LINE__);
	SIM_EXPECT_EQ(device.function.FreeSlots(), 8);
}


// A bus reset and the configuration that follows it drop the queued reports, so nothing from the old session is sent.
void CheckBusResetDropsQueue() {
	KeyboardDevice device;
	device.Configure();
	SimUSBDeviceAdapter& sim = device.sim;

	SIM_EXPECT_EQ(device.function.SendReport(ReportA), ResultStatus::ok);
	SIM_EXPECT_EQ(device.function.SendReport(ReportB), ResultStatus::ok);
	device.Enumerate();
	SIM_EXPECT_EQ(device.function.FreeSlots(), 8);
	device.device.Process();
	SIM_EXPECT_EQ(HidTest::DrainIn(sim, 0x81).size(), 0);
}


// The vendor raw report: SendReport sends 8 bytes, and the OUT endpoint delivers 8-byte reports to the delegate. A packet of another
// length is dropped, and the endpoint is armed again for the next one.
void CheckVendorRawPaths() {
	VendorDevice device;
	record = Record();
	SIM_EXPECT_EQ(device.function.SetOutputDelegate(HidFunction<8>::OutputDelegate::Bind<&OnOutput>()), ResultStatus::ok);
	device.Configure();
	SimUSBDeviceAdapter& sim = device.sim;

	std::array<uint8, 8> data = { 1, 2, 3, 4, 5, 6, 7, 8 };
	SIM_EXPECT_EQ(device.function.SendReport(data), ResultStatus::ok);
	SIM_EXPECT_EQ(device.function.SendReport(std::span<const uint8>(data).first(4)), ResultStatus::invalidParameter);
	device.device.Process();
	std::vector<std::vector<uint8>> sent = HidTest::DrainIn(sim, 0x81);
	HidTest::ExpectReports(sent, { Bytes({ 1, 2, 3, 4, 5, 6, 7, 8 }) }, "vendor report", __LINE__);

	std::array<uint8, 8> out = { 8, 7, 6, 5, 4, 3, 2, 1 };
	SIM_EXPECT(sim.HostOut(0x02, out).IsOk());
	SIM_EXPECT_EQ(record.outputs.size(), 1);
	SIM_EXPECT_BYTES(record.outputs[0], out);

	std::array<uint8, 4> shortOut = { 9, 9, 9, 9 };
	SIM_EXPECT(sim.HostOut(0x02, shortOut).IsOk());
	SIM_EXPECT_EQ(record.outputs.size(), 1);

	SIM_EXPECT(sim.HostOut(0x02, out).IsOk());
	SIM_EXPECT_EQ(record.outputs.size(), 2);
}


// The keyboard with an interrupt OUT endpoint: a one-byte LED report arrives on the endpoint and reaches the delegate.
void CheckOutEndpoint() {
	KeyboardOutDevice device;
	record = Record();
	SIM_EXPECT_EQ(device.function.SetOutputDelegate(HidFunction<8>::OutputDelegate::Bind<&OnOutput>()), ResultStatus::ok);
	device.Configure();
	SimUSBDeviceAdapter& sim = device.sim;

	std::array<uint8, 1> leds = { 0x02 };
	SIM_EXPECT(sim.HostOut(0x02, leds).IsOk());
	SIM_EXPECT_EQ(record.outputs.size(), 1);
	SIM_EXPECT_BYTES(record.outputs[0], leds);

	// A zero-length packet carries no report: nothing is delivered, and the endpoint takes the next one.
	SIM_EXPECT(sim.HostOut(0x02, std::span<const uint8>()).IsOk());
	SIM_EXPECT_EQ(record.outputs.size(), 1);

	std::array<uint8, 1> next = { 0x04 };
	SIM_EXPECT(sim.HostOut(0x02, next).IsOk());
	SIM_EXPECT_EQ(record.outputs.size(), 2);
	SIM_EXPECT_BYTES(record.outputs[1], next);
}


// A set-delegate call after configuration is refused, because the callbacks run from the device's context.
void CheckDelegatesBeforeStart() {
	IdentifiedDevice device;
	SIM_EXPECT_EQ(device.function.SetOutputDelegate(HidFunction<8>::OutputDelegate::Bind<&OnOutput>()), ResultStatus::ok);
	device.Configure();
	SIM_EXPECT_EQ(device.function.SetOutputDelegate(HidFunction<8>::OutputDelegate::Bind<&OnOutput>()), ResultStatus::busy);
	SIM_EXPECT_EQ(device.function.SetFeatureSetDelegate(HidFunction<8>::FeatureSetDelegate::Bind<&OnFeatureSet>()), ResultStatus::busy);
}


// The idle period is a lower bound as well as a rate: no repeat before the period has passed, and one after it. The period is 50 units
// of 4 ms, so the check right after SET_IDLE has 200 ms of margin.
void CheckIdleWaitsForPeriod() {
	KeyboardDevice device;
	device.Configure();
	SimUSBDeviceAdapter& sim = device.sim;

	SIM_EXPECT_EQ(device.function.SendReport(ReportA), ResultStatus::ok);
	device.device.Process();
	SIM_EXPECT_EQ(HidTest::DrainIn(sim, 0x81).size(), 1);

	SIM_EXPECT_EQ(NoData(sim, SimHost::MakeSetup(0x21, SetIdle, static_cast<uint16>(50 << 8), 0, 0)), ResultStatus::ok);
	device.device.Process();
	SIM_EXPECT_EQ(HidTest::DrainIn(sim, 0x81).size(), 0);

	std::this_thread::sleep_for(std::chrono::milliseconds(220));
	device.device.Process();
	HidTest::ExpectReports(HidTest::DrainIn(sim, 0x81), { Bytes({ 0x02, 0x00, 0x04, 0x05, 0x00, 0x00, 0x00, 0x00 }) },
		"one repeat after the period", __LINE__);

	// The period starts again from the repeat, so the next Process() sends nothing.
	device.device.Process();
	SIM_EXPECT_EQ(HidTest::DrainIn(sim, 0x81).size(), 0);
}


// A repeat is not queued while a report of its ID waits: the waiting report is the newer one, and the repeat would send it twice.
void CheckIdleSkipsQueuedId() {
	KeyboardDevice device;
	device.Configure();
	SimUSBDeviceAdapter& sim = device.sim;

	SIM_EXPECT_EQ(device.function.SendReport(ReportA), ResultStatus::ok);
	device.device.Process();
	SIM_EXPECT_EQ(HidTest::DrainIn(sim, 0x81).size(), 1);

	SIM_EXPECT_EQ(NoData(sim, SimHost::MakeSetup(0x21, SetIdle, static_cast<uint16>(1 << 8), 0, 0)), ResultStatus::ok);
	SIM_EXPECT_EQ(device.function.SendReport(ReportB), ResultStatus::ok);
	std::this_thread::sleep_for(std::chrono::milliseconds(12));

	// The period has passed, but ReportB of ID 0 waits in the queue, so only that one is sent.
	device.device.Process();
	HidTest::ExpectReports(HidTest::DrainIn(sim, 0x81), { Bytes({ 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00 }) },
		"waiting report and no repeat", __LINE__);
}


// The boot cut is made when a report is sent, so a protocol switch covers the reports already queued: a report queued in report protocol
// is cut when boot is selected before it goes out, and a report queued in boot protocol goes out whole after the switch back.
void CheckBootLengthAtSend() {
	MouseDevice mouse;
	mouse.Configure();
	SimUSBDeviceAdapter& sim = mouse.sim;
	std::array<uint8, 4> report = { 0x01, 0x05, 0xFE, 0x02 };

	SIM_EXPECT_EQ(mouse.function.SendReport(report), ResultStatus::ok);
	SIM_EXPECT_EQ(NoData(sim, SimHost::MakeSetup(0x21, SetProtocol, 0, 0, 0)), ResultStatus::ok);
	mouse.device.Process();
	HidTest::ExpectReports(HidTest::DrainIn(sim, 0x81), { Bytes({ 0x01, 0x05, 0xFE }) }, "queued in report, sent in boot", __LINE__);

	SIM_EXPECT_EQ(mouse.function.SendReport(report), ResultStatus::ok);
	SIM_EXPECT_EQ(NoData(sim, SimHost::MakeSetup(0x21, SetProtocol, 1, 0, 0)), ResultStatus::ok);
	mouse.device.Process();
	HidTest::ExpectReports(HidTest::DrainIn(sim, 0x81), { Bytes({ 0x01, 0x05, 0xFE, 0x02 }) }, "queued in boot, sent in report", __LINE__);
}


// A device context that accepts every call and counts the endpoints opened. OnConfigured needs one, and the layout check does not
// need a device.
class AcceptingContext : public USBDeviceContext {
public:
	uint32 opened = 0;


	ResultStatus OpenEndpoint(const USBDeviceAdapter::EndpointConfig&) override {
		opened++;
		return ResultStatus::ok;
	}


	ResultStatus CloseEndpoint(uint8) override {
		return ResultStatus::ok;
	}


	ResultStatus StartInTransfer(uint8, std::span<const uint8>, bool) override {
		return ResultStatus::ok;
	}


	ResultStatus StartOutTransfer(uint8, std::span<uint8>) override {
		return ResultStatus::ok;
	}


	ResultStatus Stall(uint8) override {
		return ResultStatus::ok;
	}


	ResultStatus ClearStall(uint8) override {
		return ResultStatus::ok;
	}


	ResultStatus RemoteWakeup() override {
		return ResultStatus::ok;
	}


	ResultStatus ControlSendData(std::span<const uint8>) override {
		return ResultStatus::ok;
	}


	ResultStatus ControlReceiveData(std::span<uint8>) override {
		return ResultStatus::ok;
	}
};


// The vendor interface's descriptors written out by hand: interface, HID descriptor with a 27-byte report, interrupt IN 0x81 and
// interrupt OUT 0x02, each with the packet size given as two bytes.
std::array<uint8, 32> VendorDescriptors(uint16 inPacket, uint16 outPacket) {
	std::array<uint8, 32> bytes = {
		0x09, 0x04, 0x00, 0x00, 0x02, 0x03, 0x00, 0x00, 0x00,
		0x09, 0x21, 0x11, 0x01, 0x00, 0x01, 0x22, 0x1B, 0x00,
		0x07, 0x05, 0x81, 0x03, static_cast<uint8>(inPacket & 0xFF), static_cast<uint8>(inPacket >> 8), 0x01,
		0x07, 0x05, 0x02, 0x03, static_cast<uint8>(outPacket & 0xFF), static_cast<uint8>(outPacket >> 8), 0x01
	};
	return bytes;
}


// Packets of 64 bytes are the largest the function serves, on both endpoints. A packet of 128 bytes on either one is refused by
// OnConfigured, which opens nothing.
void CheckPacketSizeBounds() {
	const std::array<uint8, 1> interfaces = { 0x00 };
	const std::array<uint8, 2> endpoints = { 0x81, 0x02 };

	std::array<uint8, 32> largest = VendorDescriptors(0x40, 0x40);
	USBFunctionLayout accepted{ interfaces, endpoints, largest };
	HidFunction<8> acceptedFunction(accepted, HidTest::VendorReport::Info);
	AcceptingContext acceptedContext;
	SIM_EXPECT_EQ(acceptedFunction.OnConfigured(acceptedContext), ResultStatus::ok);
	SIM_EXPECT_EQ(acceptedContext.opened, 2);

	std::array<uint8, 32> oversizedOut = VendorDescriptors(0x08, 0x80);
	USBFunctionLayout refusedOutLayout{ interfaces, endpoints, oversizedOut };
	HidFunction<8> refusedOutFunction(refusedOutLayout, HidTest::VendorReport::Info);
	AcceptingContext refusedOutContext;
	SIM_EXPECT_EQ(refusedOutFunction.OnConfigured(refusedOutContext), ResultStatus::invalidParameter);
	SIM_EXPECT_EQ(refusedOutContext.opened, 0);

	std::array<uint8, 32> oversizedIn = VendorDescriptors(0x80, 0x40);
	USBFunctionLayout refusedInLayout{ interfaces, endpoints, oversizedIn };
	HidFunction<8> refusedInFunction(refusedInLayout, HidTest::VendorReport::Info);
	AcceptingContext refusedInContext;
	SIM_EXPECT_EQ(refusedInFunction.OnConfigured(refusedInContext), ResultStatus::invalidParameter);
	SIM_EXPECT_EQ(refusedInContext.opened, 0);
}


}


int main() {
	CheckNotConfigured();
	CheckDescriptorRequests();
	CheckGetReportInput();
	CheckSetReportOutput();
	CheckFeatureReports();
	CheckReportIdsAndLengths();
	CheckIdleRequests();
	CheckProtocolRequests();
	CheckBootProtocolReports();
	CheckQueueFull();
	CheckIdleRepeat();
	CheckRemoteWakeup();
	CheckHalt();
	CheckBusResetDropsQueue();
	CheckVendorRawPaths();
	CheckOutEndpoint();
	CheckDelegatesBeforeStart();
	CheckIdleWaitsForPeriod();
	CheckIdleSkipsQueuedId();
	CheckBootLengthAtSend();
	CheckPacketSizeBounds();
	return SimTest::Finish("TestHid");
}
