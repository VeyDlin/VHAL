#include <VHAL.h>
#include "CdcTestDevices.h"
#include "SimAssert.h"
#include "SimHost.h"
#include <Drivers/Interface/USB/Class/CDC/USBCdcProtocol.h>
#include <array>
#include <cstddef>
#include <span>


// The class requests of the communication interface, through the simulated controller. The requests and their bytes are from PSTN 1.2
// section 6.3: SET_LINE_CODING (0x20) and GET_LINE_CODING (0x21) carry the 7-byte line coding, SET_CONTROL_LINE_STATE (0x22) carries DTR
// in bit 0 and RTS in bit 1, and SEND_BREAK (0x23) carries the duration in milliseconds. A request to the data interface, a wrong
// direction, a wrong length or an unknown request is stalled.
namespace {


// The recorders the delegates write into. The delegates are plain function pointers with a context, so they never allocate.
struct Recorder {
	uint32 lineCodingCalls = 0;
	USBCdcLineCoding lastCoding{};
	uint32 controlCalls = 0;
	bool lastDtr = false;
	bool lastRts = false;
	uint32 breakCalls = 0;
	uint16 lastBreak = 0;
};


void RecordLineCoding(void* context, const USBCdcLineCoding& coding) {
	Recorder& recorder = *static_cast<Recorder*>(context);
	recorder.lineCodingCalls++;
	recorder.lastCoding = coding;
}


void RecordControlLines(void* context, bool dtr, bool rts) {
	Recorder& recorder = *static_cast<Recorder*>(context);
	recorder.controlCalls++;
	recorder.lastDtr = dtr;
	recorder.lastRts = rts;
}


void RecordBreak(void* context, uint16 durationMs) {
	Recorder& recorder = *static_cast<Recorder*>(context);
	recorder.breakCalls++;
	recorder.lastBreak = durationMs;
}


// Sets the three delegates of the function. They are set before the device starts, as the device requires.
void SetHandlers(CdcTest::Function& function, Recorder& recorder) {
	SIM_EXPECT_EQ(function.SetLineCodingHandler(CdcTest::Function::LineCodingDelegate::FromRaw(&RecordLineCoding, &recorder)), ResultStatus::ok);
	SIM_EXPECT_EQ(function.SetControlLineStateHandler(CdcTest::Function::ControlLineDelegate::FromRaw(&RecordControlLines, &recorder)), ResultStatus::ok);
	SIM_EXPECT_EQ(function.SetBreakHandler(CdcTest::Function::BreakDelegate::FromRaw(&RecordBreak, &recorder)), ResultStatus::ok);
}


// The line coding a GET_LINE_CODING answers with, and the one SET_LINE_CODING sends. 115200 is 0x0001C200 little-endian.
constexpr uint8 DefaultLineCoding[] = {
	0x00, 0xC2, 0x01, 0x00,		// dwDTERate 115200
	0x00,						// bCharFormat: one stop bit
	0x00,						// bParityType: none
	0x08						// bDataBits: 8
};


// 57600 baud (0x0000E100), two stop bits, even parity, seven data bits.
constexpr uint8 SevenEvenLineCoding[] = {
	0x00, 0xE1, 0x00, 0x00,		// dwDTERate 57600
	0x02,						// bCharFormat: two stop bits
	0x02,						// bParityType: even
	0x07						// bDataBits: 7
};


void CheckGetDefaultLineCoding(CdcFixture& fixture) {
	std::array<uint8, 16> in = {};
	uint32 received = 0;
	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0xA1, 0x21, 0x0000, 0x0000, 7), std::span<const uint8>(), in, received), ResultStatus::ok);
	SIM_EXPECT_EQ(received, 7);
	SIM_EXPECT_BYTES(std::span<const uint8>(in).first(received), DefaultLineCoding);
}


void TestDefaultLineCodingAndGet() {
	CdcFixture fixture;
	Recorder recorder;
	SetHandlers(fixture.function, recorder);
	fixture.Configure();

	CheckGetDefaultLineCoding(fixture);
	SIM_EXPECT_EQ(recorder.lineCodingCalls, 0);
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


void TestSetLineCodingThenGet() {
	CdcFixture fixture;
	Recorder recorder;
	SetHandlers(fixture.function, recorder);
	fixture.Configure();

	// SET_LINE_CODING: the host sends the 7 bytes in the data stage. The handler gets the decoded values once, after they are stored.
	uint32 received = 0;
	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x21, 0x20, 0x0000, 0x0000, 7), SevenEvenLineCoding, std::span<uint8>(), received), ResultStatus::ok);
	SIM_EXPECT_EQ(recorder.lineCodingCalls, 1);
	SIM_EXPECT_EQ(recorder.lastCoding.dteRate, 57600U);
	SIM_EXPECT_EQ(recorder.lastCoding.stopBits, 2);
	SIM_EXPECT_EQ(recorder.lastCoding.parity, 2);
	SIM_EXPECT_EQ(recorder.lastCoding.dataBits, 7);
	SIM_EXPECT(fixture.function.GetLineCoding().dteRate == 57600U);

	// GET_LINE_CODING answers with the stored coding, not the default.
	std::array<uint8, 16> in = {};
	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0xA1, 0x21, 0x0000, 0x0000, 7), std::span<const uint8>(), in, received), ResultStatus::ok);
	SIM_EXPECT_BYTES(std::span<const uint8>(in).first(received), SevenEvenLineCoding);
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


void TestControlLineState() {
	CdcFixture fixture;
	Recorder recorder;
	SetHandlers(fixture.function, recorder);
	fixture.Configure();

	// DTR and RTS together: the terminal is open.
	SIM_EXPECT(!fixture.function.IsTerminalOpen());
	SIM_EXPECT_EQ(fixture.SetControlLines(USBCdcControlDtr | USBCdcControlRts), ResultStatus::ok);
	SIM_EXPECT(fixture.function.IsTerminalOpen());
	SIM_EXPECT_EQ(recorder.controlCalls, 1);
	SIM_EXPECT(recorder.lastDtr);
	SIM_EXPECT(recorder.lastRts);

	// RTS alone: the terminal is closed, and the handler sees the change.
	SIM_EXPECT_EQ(fixture.SetControlLines(USBCdcControlRts), ResultStatus::ok);
	SIM_EXPECT(!fixture.function.IsTerminalOpen());
	SIM_EXPECT_EQ(recorder.controlCalls, 2);
	SIM_EXPECT(!recorder.lastDtr);
	SIM_EXPECT(recorder.lastRts);

	// The same value again changes nothing, so the handler is not called.
	SIM_EXPECT_EQ(fixture.SetControlLines(USBCdcControlRts), ResultStatus::ok);
	SIM_EXPECT_EQ(recorder.controlCalls, 2);

	// Bits above RTS are reserved and ignored: 0x0004 alone clears both lines.
	SIM_EXPECT_EQ(fixture.SetControlLines(0x0004), ResultStatus::ok);
	SIM_EXPECT_EQ(recorder.controlCalls, 3);
	SIM_EXPECT(!recorder.lastDtr);
	SIM_EXPECT(!recorder.lastRts);
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


void TestSendBreak() {
	CdcFixture fixture;
	Recorder recorder;
	SetHandlers(fixture.function, recorder);
	fixture.Configure();

	// SEND_BREAK with the duration in wValue: 256 ms, then 0xFFFF (until the next SEND_BREAK), then 0 (end the break).
	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x21, 0x23, 0x0100, 0x0000, 0)), ResultStatus::ok);
	SIM_EXPECT_EQ(recorder.breakCalls, 1);
	SIM_EXPECT_EQ(recorder.lastBreak, 256);
	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x21, 0x23, 0xFFFF, 0x0000, 0)), ResultStatus::ok);
	SIM_EXPECT_EQ(recorder.lastBreak, 0xFFFF);
	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x21, 0x23, 0x0000, 0x0000, 0)), ResultStatus::ok);
	SIM_EXPECT_EQ(recorder.breakCalls, 3);
	SIM_EXPECT_EQ(recorder.lastBreak, 0);
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


// Requests that must be stalled: the data interface has none, a wrong length or direction is refused, and so is an unknown request.
// The handlers must not run for any of them, and the state stays as it was.
void TestStalledRequests() {
	CdcFixture fixture;
	Recorder recorder;
	SetHandlers(fixture.function, recorder);
	fixture.Configure();
	uint32 received = 0;
	std::array<uint8, 16> in = {};

	// The data interface (1) has no class requests of its own.
	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x21, 0x22, 0x0003, 0x0001, 0)), ResultStatus::accessError);
	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0xA1, 0x21, 0x0000, 0x0001, 7), std::span<const uint8>(), in, received), ResultStatus::accessError);

	// SET_LINE_CODING of 6 bytes is not the 7-byte block.
	std::array<uint8, 6> shortBlock = {};
	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x21, 0x20, 0x0000, 0x0000, 6), shortBlock, std::span<uint8>(), received), ResultStatus::accessError);

	// GET_LINE_CODING sent as host to device, and SET_CONTROL_LINE_STATE with a data stage, are not requests this function has.
	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x21, 0x21, 0x0000, 0x0000, 7), SevenEvenLineCoding, std::span<uint8>(), received), ResultStatus::accessError);
	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x21, 0x22, 0x0003, 0x0000, 2), std::span<const uint8>(), std::span<uint8>(), received), ResultStatus::accessError);

	// An unknown class request, and a class request addressed to the device rather than the interface.
	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x21, 0x99, 0x0000, 0x0000, 0)), ResultStatus::accessError);
	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x20, 0x22, 0x0003, 0x0000, 0)), ResultStatus::accessError);

	SIM_EXPECT_EQ(recorder.lineCodingCalls, 0);
	SIM_EXPECT_EQ(recorder.controlCalls, 0);
	SIM_EXPECT_EQ(recorder.breakCalls, 0);
	SIM_EXPECT(!fixture.function.IsTerminalOpen());
	CheckGetDefaultLineCoding(fixture);
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


// SET_LINE_CODING with a block the port cannot run: the request is stalled. The rules are PSTN 1.2 section 6.3.11.
void ExpectLineCodingStalled(CdcFixture& fixture, std::span<const uint8> block) {
	uint32 received = 0;
	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x21, 0x20, 0x0000, 0x0000, 7), block, std::span<uint8>(), received), ResultStatus::accessError);
}


// A coding the port cannot run is stalled, and the stored coding stays as it was. A coding at the edge of the rules is accepted.
void TestLineCodingRangesAreChecked() {
	CdcFixture fixture;
	Recorder recorder;
	SetHandlers(fixture.function, recorder);
	fixture.Configure();
	uint32 received = 0;

	// The stored coding is 57600 7E2. A stall that stored anything would change it.
	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x21, 0x20, 0x0000, 0x0000, 7), SevenEvenLineCoding, std::span<uint8>(), received), ResultStatus::ok);
	SIM_EXPECT_EQ(recorder.lineCodingCalls, 1);

	// A zero rate, stop-bit code 3, parity code 5, and data bits 0, 4 and 9.
	std::array<uint8, 7> block = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08 };
	ExpectLineCodingStalled(fixture, block);
	block = { 0x00, 0xC2, 0x01, 0x00, 0x03, 0x00, 0x08 };
	ExpectLineCodingStalled(fixture, block);
	block = { 0x00, 0xC2, 0x01, 0x00, 0x00, 0x05, 0x08 };
	ExpectLineCodingStalled(fixture, block);
	block = { 0x00, 0xC2, 0x01, 0x00, 0x00, 0x00, 0x00 };
	ExpectLineCodingStalled(fixture, block);
	block = { 0x00, 0xC2, 0x01, 0x00, 0x00, 0x00, 0x04 };
	ExpectLineCodingStalled(fixture, block);
	block = { 0x00, 0xC2, 0x01, 0x00, 0x00, 0x00, 0x09 };
	ExpectLineCodingStalled(fixture, block);
	SIM_EXPECT_EQ(recorder.lineCodingCalls, 1);
	SIM_EXPECT(fixture.function.GetLineCoding().dteRate == 57600U);
	SIM_EXPECT_EQ(fixture.function.GetLineCoding().dataBits, 7);

	// The edges: a rate of 1, stop-bit code 2, parity code 4 and 16 data bits; then 5 data bits with the default rate.
	block = { 0x01, 0x00, 0x00, 0x00, 0x02, 0x04, 0x10 };
	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x21, 0x20, 0x0000, 0x0000, 7), block, std::span<uint8>(), received), ResultStatus::ok);
	block = { 0x00, 0xC2, 0x01, 0x00, 0x00, 0x00, 0x05 };
	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x21, 0x20, 0x0000, 0x0000, 7), block, std::span<uint8>(), received), ResultStatus::ok);
	SIM_EXPECT_EQ(recorder.lineCodingCalls, 3);
	SIM_EXPECT_EQ(fixture.function.GetLineCoding().dataBits, 5);
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


// A bus reset returns the port to its defaults: DTR and RTS drop, the handler is told about the DTR change, and the coding goes back to
// 115200 8N1. The host sets DTR again after it enumerates the device.
void TestBusResetRestoresDefaults() {
	CdcFixture fixture;
	Recorder recorder;
	SetHandlers(fixture.function, recorder);
	fixture.Configure();
	uint32 received = 0;

	SIM_EXPECT_EQ(fixture.SetControlLines(USBCdcControlDtr | USBCdcControlRts), ResultStatus::ok);
	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x21, 0x20, 0x0000, 0x0000, 7), SevenEvenLineCoding, std::span<uint8>(), received), ResultStatus::ok);
	SIM_EXPECT(fixture.function.IsTerminalOpen());
	SIM_EXPECT_EQ(recorder.controlCalls, 1);
	SIM_EXPECT_EQ(recorder.lineCodingCalls, 1);

	fixture.Enumerate();
	SIM_EXPECT(!fixture.function.IsTerminalOpen());
	SIM_EXPECT_EQ(recorder.controlCalls, 2);
	SIM_EXPECT(!recorder.lastDtr);
	SIM_EXPECT(!recorder.lastRts);
	SIM_EXPECT(fixture.function.GetLineCoding().dteRate == 115200U);
	SIM_EXPECT_EQ(fixture.function.GetLineCoding().dataBits, 8);
	CheckGetDefaultLineCoding(fixture);
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


// The handlers are refused once the device is configured, because they run from the device's context.
void TestHandlersBusyWhenConfigured() {
	CdcFixture fixture;
	fixture.Configure();
	Recorder recorder;
	SIM_EXPECT_EQ(fixture.function.SetLineCodingHandler(CdcTest::Function::LineCodingDelegate::FromRaw(&RecordLineCoding, &recorder)), ResultStatus::busy);
	SIM_EXPECT_EQ(fixture.function.SetControlLineStateHandler(CdcTest::Function::ControlLineDelegate::FromRaw(&RecordControlLines, &recorder)), ResultStatus::busy);
	SIM_EXPECT_EQ(fixture.function.SetBreakHandler(CdcTest::Function::BreakDelegate::FromRaw(&RecordBreak, &recorder)), ResultStatus::busy);
}


}


int main() {
	TestDefaultLineCodingAndGet();
	TestSetLineCodingThenGet();
	TestControlLineState();
	TestSendBreak();
	TestStalledRequests();
	TestLineCodingRangesAreChecked();
	TestBusResetRestoresDefaults();
	TestHandlersBusyWhenConfigured();
	return SimTest::Finish("TestCdcAcmRequests");
}
