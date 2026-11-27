#include <VHAL.h>
#include "CdcTestDevices.h"
#include "SimAssert.h"
#include "SimHost.h"
#include <Drivers/Interface/USB/Class/CDC/USBCdcProtocol.h>
#include <array>
#include <cstddef>
#include <span>


// SERIAL_STATE notifications on the interrupt IN endpoint 0x83 (PSTN 1.2 section 6.3.5): bmRequestType 0xA1, bNotification 0x20,
// wValue 0, wIndex the communication interface (0), wLength 2, then the UART state bitmap, little-endian. The expected bytes are
// written here, not generated from the function's helper.
namespace {


// The notification for a state, as the host reads it.
void ExpectNotification(CdcFixture& fixture, uint16 state) {
	std::array<uint8, 10> expected = {
		0xA1, 0x20,				// bmRequestType 0xA1, bNotification SERIAL_STATE
		0x00, 0x00,				// wValue 0
		0x00, 0x00,				// wIndex: communication interface 0
		0x02, 0x00,				// wLength 2
		static_cast<uint8>(state & 0xFF), static_cast<uint8>(state >> 8)
	};

	std::array<uint8, 16> in = {};
	uint32 received = 0;
	SIM_EXPECT_EQ(fixture.sim.HostIn(0x83, in, received), ResultStatus::ok);
	SIM_EXPECT_EQ(received, 10);
	SIM_EXPECT_BYTES(std::span<const uint8>(in).first(received), expected);
}


// After configuration the current state is sent once, so the host learns it. Here it is the zero state.
void TestInitialStateAfterConfiguration() {
	CdcFixture fixture;
	fixture.Configure();
	SIM_EXPECT(!fixture.sim.HasArmedTransfer(0x83));

	fixture.device.Process();
	SIM_EXPECT(fixture.sim.HasArmedTransfer(0x83));
	ExpectNotification(fixture, 0x0000);
	SIM_EXPECT(!fixture.sim.HasArmedTransfer(0x83));

	// Nothing has changed, so nothing more is sent.
	fixture.device.Process();
	SIM_EXPECT(!fixture.sim.HasArmedTransfer(0x83));
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


void TestStateChangeIsSent() {
	CdcFixture fixture;
	fixture.Configure();
	fixture.device.Process();
	ExpectNotification(fixture, 0x0000);

	// DCD and DSR set: bits 0 and 1 of the bitmap.
	fixture.function.SetSerialState(USBCdcUartRxCarrier | USBCdcUartTxCarrier);
	fixture.device.Process();
	SIM_EXPECT(fixture.sim.HasArmedTransfer(0x83));
	ExpectNotification(fixture, 0x0003);

	// The same state again is not sent.
	fixture.function.SetSerialState(USBCdcUartRxCarrier | USBCdcUartTxCarrier);
	fixture.device.Process();
	SIM_EXPECT(!fixture.sim.HasArmedTransfer(0x83));
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


// A state that changes while a notification is in flight is not written into its buffer. The newest state is sent when the one in
// flight completes, from the interrupt, so the host sees it without another Process().
void TestChangeWhileInFlightSendsNewest() {
	CdcFixture fixture;
	fixture.Configure();
	fixture.device.Process();
	ExpectNotification(fixture, 0x0000);

	fixture.function.SetSerialState(USBCdcUartBreak);
	fixture.device.Process();
	SIM_EXPECT(fixture.sim.HasArmedTransfer(0x83));

	fixture.function.SetSerialState(USBCdcUartBreak | USBCdcUartOverRun);
	fixture.function.SetSerialState(USBCdcUartBreak | USBCdcUartParity);
	fixture.device.Process();

	// The in-flight notification is the break alone, and it completes with it. The next one is armed at once, with the newest state.
	std::array<uint8, 16> in = {};
	uint32 received = 0;
	SIM_EXPECT_EQ(fixture.sim.HostIn(0x83, in, received), ResultStatus::ok);
	SIM_EXPECT_EQ(in[8], USBCdcUartBreak & 0xFF);
	SIM_EXPECT(fixture.sim.HasArmedTransfer(0x83));
	ExpectNotification(fixture, USBCdcUartBreak | USBCdcUartParity);
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


// A halt of the notification endpoint abandons the notification in flight and holds the state. The clear sends the current state,
// which is the one set during the halt, not the lost one.
void TestHaltAbandonsAndClearSendsCurrent() {
	CdcFixture fixture;
	fixture.Configure();
	fixture.device.Process();
	ExpectNotification(fixture, 0x0000);

	fixture.function.SetSerialState(USBCdcUartRingSignal);
	fixture.device.Process();
	SIM_EXPECT(fixture.sim.HasArmedTransfer(0x83));

	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x02, 3, 0x0000, 0x0083, 0)), ResultStatus::ok);
	SIM_EXPECT(!fixture.sim.HasArmedTransfer(0x83));

	fixture.function.SetSerialState(USBCdcUartFraming);
	fixture.device.Process();
	SIM_EXPECT(!fixture.sim.HasArmedTransfer(0x83));

	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x02, 1, 0x0000, 0x0083, 0)), ResultStatus::ok);
	SIM_EXPECT(fixture.sim.HasArmedTransfer(0x83));
	ExpectNotification(fixture, USBCdcUartFraming);
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


// A deconfiguration stops the notifications; the next configuration sends the current state once more.
void TestReconfigureResendsState() {
	CdcFixture fixture;
	fixture.Configure();
	fixture.device.Process();
	ExpectNotification(fixture, 0x0000);

	fixture.function.SetSerialState(USBCdcUartOverRun);
	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x00, 9, 0x0000, 0x0000, 0)), ResultStatus::ok);
	fixture.device.Process();
	SIM_EXPECT(!fixture.sim.HasArmedTransfer(0x83));

	fixture.Enumerate();
	fixture.device.Process();
	ExpectNotification(fixture, USBCdcUartOverRun);
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


}


int main() {
	TestInitialStateAfterConfiguration();
	TestStateChangeIsSent();
	TestChangeWhileInFlightSendsNewest();
	TestHaltAbandonsAndClearSendsCurrent();
	TestReconfigureResendsState();
	return SimTest::Finish("TestCdcAcmNotification");
}
