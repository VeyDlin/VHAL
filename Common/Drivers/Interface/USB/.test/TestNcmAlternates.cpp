#include <VHAL.h>
#include "NcmTestDevices.h"
#include "SimAssert.h"
#include "SimHost.h"
#include <Drivers/Interface/USB/Class/CDC/USBCdcProtocol.h>
#include <array>
#include <cstddef>
#include <span>


// The data interface's alternate settings and the notifications they cause. Alternate 1 opens the bulk pair and starts the session:
// CONNECTION_SPEED_CHANGE, then NETWORK_CONNECTION(1), both on the notification endpoint 0x83 of NcmTest::ExplicitUsb. Alternate 0
// closes the bulk pair and sends NETWORK_CONNECTION(0) when the host had seen the link up. The bytes below are written by hand from
// NCM 1.0 section 6.3: the header is bmRequestType 0xA1, the code, wValue, wIndex 0 (the communication interface), wLength.
namespace {


// NETWORK_CONNECTION(1) and CONNECTION_SPEED_CHANGE to 12 000 000 bit/s, both directions (0x00B71B00).
constexpr uint8 SpeedChangeBytes[16] = {
	0xA1, 0x2A, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00,
	0x00, 0x1B, 0xB7, 0x00,
	0x00, 0x1B, 0xB7, 0x00
};


constexpr uint8 ConnectedBytes[8] = {0xA1, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00};


constexpr uint8 DisconnectedBytes[8] = {0xA1, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};


// The counts of the function's delegates. They are plain counters, so the delegates never allocate.
struct Events {
	uint32 started = 0;
	uint32 stopped = 0;
};


void CountStarted(void* context) {
	static_cast<Events*>(context)->started++;
}


void CountStopped(void* context) {
	static_cast<Events*>(context)->stopped++;
}


void SetEvents(NcmFixture& fixture, Events& events) {
	SIM_EXPECT_EQ(fixture.function.SetDataPathStartedHandler(NcmTest::Function::DataPathDelegate::FromRaw(&CountStarted, &events)), ResultStatus::ok);
	SIM_EXPECT_EQ(fixture.function.SetDataPathStoppedHandler(NcmTest::Function::DataPathDelegate::FromRaw(&CountStopped, &events)), ResultStatus::ok);
}


// A 14-byte Ethernet header with no payload: the smallest frame the function sends.
constexpr uint8 HeaderFrame[14] = {
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0x02, 0x00, 0x00, 0x00, 0x00, 0x01,
	0x08, 0x00
};


// Takes the armed notification and checks its bytes. The packet is the whole notification, so its length is checked too.
template<std::size_t Size>
void ExpectNotification(NcmFixture& fixture, const uint8 (&expected)[Size]) {
	SIM_EXPECT(fixture.sim.HasArmedTransfer(0x83));
	std::array<uint8, 16> in = {};
	uint32 received = 0;
	SIM_EXPECT_EQ(fixture.sim.HostIn(0x83, in, received), ResultStatus::ok);
	SIM_EXPECT_EQ(received, Size);
	for (std::size_t index = 0; index < Size; index++) {
		SIM_EXPECT_EQ(in[index], expected[index]);
	}
}


// Alternate 1 opens the bulk IN and OUT endpoints and arms the OUT endpoint; alternate 0 closes them. The notification endpoint stays open.
void TestAlternateOpensAndClosesEndpoints() {
	NcmFixture fixture;
	fixture.Configure();
	SIM_EXPECT(fixture.sim.FindEndpoint(0x81) == nullptr);
	SIM_EXPECT(fixture.sim.FindEndpoint(0x02) == nullptr);
	SIM_EXPECT(fixture.sim.FindEndpoint(0x83) != nullptr);
	SIM_EXPECT(!fixture.function.IsDataPathActive());

	SIM_EXPECT_EQ(fixture.SelectAlternate(1), ResultStatus::ok);
	SIM_EXPECT(fixture.function.IsDataPathActive());
	SIM_EXPECT(fixture.sim.FindEndpoint(0x81) != nullptr);
	SIM_EXPECT(fixture.sim.FindEndpoint(0x02) != nullptr);
	SIM_EXPECT(fixture.sim.HasArmedTransfer(0x02));
	const USBDeviceAdapter::EndpointConfig* bulkIn = fixture.sim.FindEndpoint(0x81);
	const USBDeviceAdapter::EndpointConfig* bulkOut = fixture.sim.FindEndpoint(0x02);
	if (bulkIn != nullptr && bulkOut != nullptr) {
		SIM_EXPECT_EQ(bulkIn->maxPacketSize, 64);
		SIM_EXPECT_EQ(bulkOut->maxPacketSize, 64);
	}

	SIM_EXPECT_EQ(fixture.SelectAlternate(0), ResultStatus::ok);
	SIM_EXPECT(!fixture.function.IsDataPathActive());
	SIM_EXPECT(fixture.sim.FindEndpoint(0x81) == nullptr);
	SIM_EXPECT(fixture.sim.FindEndpoint(0x02) == nullptr);
	SIM_EXPECT(!fixture.sim.HasArmedTransfer(0x02));
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


// Only alternates 0 and 1 exist. The communication interface has no alternate setting, so SET_INTERFACE to 1 on it is stalled.
void TestOnlyTwoAlternates() {
	NcmFixture fixture;
	fixture.Configure();
	SIM_EXPECT_EQ(fixture.SelectAlternate(2), ResultStatus::accessError);
	SIM_EXPECT(!fixture.function.IsDataPathActive());
	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x01, 11, 1, 0x0000, 0)), ResultStatus::accessError);
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


// GET_INTERFACE of the data interface answers the selected alternate; the communication interface answers 0.
void TestGetInterface() {
	NcmFixture fixture;
	fixture.Configure();

	std::array<uint8, 1> in = {0xFF};
	uint32 received = 0;
	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x81, 10, 0x0000, 0x0001, 1), std::span<const uint8>(), in, received), ResultStatus::ok);
	SIM_EXPECT_EQ(in[0], 0);

	SIM_EXPECT_EQ(fixture.SelectAlternate(1), ResultStatus::ok);
	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x81, 10, 0x0000, 0x0001, 1), std::span<const uint8>(), in, received), ResultStatus::ok);
	SIM_EXPECT_EQ(in[0], 1);

	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x81, 10, 0x0000, 0x0000, 1), std::span<const uint8>(), in, received), ResultStatus::ok);
	SIM_EXPECT_EQ(in[0], 0);
}


// Opening the path sends the speed change, then the connection. The next notification starts when the host has taken the one in flight.
void TestNotificationOrderOnOpen() {
	NcmFixture fixture;
	fixture.Configure();
	SIM_EXPECT(!fixture.sim.HasArmedTransfer(0x83));

	SIM_EXPECT_EQ(fixture.SelectAlternate(1), ResultStatus::ok);
	ExpectNotification(fixture, SpeedChangeBytes);
	ExpectNotification(fixture, ConnectedBytes);
	SIM_EXPECT(!fixture.sim.HasArmedTransfer(0x83));
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


// Closing the path sends NETWORK_CONNECTION(0). A link set down while the path is open is sent by the next Process(), and so is the
// link up that follows it.
void TestLinkDownAndUp() {
	NcmFixture fixture;
	fixture.Configure();
	SIM_EXPECT_EQ(fixture.SelectAlternate(1), ResultStatus::ok);
	ExpectNotification(fixture, SpeedChangeBytes);
	ExpectNotification(fixture, ConnectedBytes);

	fixture.function.SetLinkUp(false);
	SIM_EXPECT(!fixture.sim.HasArmedTransfer(0x83));
	fixture.device.Process();
	ExpectNotification(fixture, DisconnectedBytes);

	fixture.function.SetLinkUp(true);
	fixture.device.Process();
	ExpectNotification(fixture, ConnectedBytes);

	// Closing the path while the link is up sends the link down, as the host has seen it up.
	SIM_EXPECT_EQ(fixture.SelectAlternate(0), ResultStatus::ok);
	ExpectNotification(fixture, DisconnectedBytes);
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


// The link goes down and back up before the next Process(), while the host holds it up and nothing is pending. The host already holds
// "up", so the request is satisfied by the time Process() runs: nothing is sent, and the link stays up.
void TestLinkToggleWithHostUpSendsNothing() {
	NcmFixture fixture;
	fixture.Configure();
	SIM_EXPECT_EQ(fixture.SelectAlternate(1), ResultStatus::ok);
	ExpectNotification(fixture, SpeedChangeBytes);
	ExpectNotification(fixture, ConnectedBytes);

	fixture.function.SetLinkUp(false);
	fixture.function.SetLinkUp(true);
	fixture.device.Process();
	SIM_EXPECT(!fixture.sim.HasArmedTransfer(0x83));
	SIM_EXPECT(fixture.function.IsLinkUp());
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


// A link that is down when the path opens is announced as down, after the speed change.
void TestLinkDownBeforeOpen() {
	NcmFixture fixture;
	fixture.Configure();
	fixture.function.SetLinkUp(false);
	SIM_EXPECT(!fixture.function.IsLinkUp());

	SIM_EXPECT_EQ(fixture.SelectAlternate(1), ResultStatus::ok);
	ExpectNotification(fixture, SpeedChangeBytes);
	ExpectNotification(fixture, DisconnectedBytes);

	// The path closed with the link down sends nothing: the host has seen the link down already.
	SIM_EXPECT_EQ(fixture.SelectAlternate(0), ResultStatus::ok);
	SIM_EXPECT(!fixture.sim.HasArmedTransfer(0x83));
	fixture.device.Process();
	SIM_EXPECT(!fixture.sim.HasArmedTransfer(0x83));
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


// The notification in flight holds the endpoint: a link change made meanwhile is sent only after the host takes the one in flight.
// The link must have been up before it goes down: the host is only told of a change from the state it last saw.
void TestChangeWaitsForInFlight() {
	NcmFixture fixture;
	fixture.Configure();
	SIM_EXPECT_EQ(fixture.SelectAlternate(1), ResultStatus::ok);
	ExpectNotification(fixture, SpeedChangeBytes);
	ExpectNotification(fixture, ConnectedBytes);

	fixture.function.SetLinkUp(false);
	fixture.device.Process();
	SIM_EXPECT(fixture.sim.HasArmedTransfer(0x83));

	// The link comes back while the link-down notification is in flight. Process() does not start a second notification.
	fixture.function.SetLinkUp(true);
	fixture.device.Process();
	ExpectNotification(fixture, DisconnectedBytes);
	ExpectNotification(fixture, ConnectedBytes);
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


// Deconfiguration ends the session: the path closes, the stop event runs, and the frames queued for the old session are dropped. The next
// configuration starts with the link down and sends nothing until the path opens again.
void TestDeconfigureEndsSession() {
	NcmFixture fixture;
	Events events;
	SetEvents(fixture, events);
	fixture.Configure();
	SIM_EXPECT_EQ(fixture.SelectAlternate(1), ResultStatus::ok);
	SIM_EXPECT_EQ(events.started, 1);
	ExpectNotification(fixture, SpeedChangeBytes);
	ExpectNotification(fixture, ConnectedBytes);

	SIM_EXPECT_EQ(fixture.function.SendFrame(HeaderFrame), ResultStatus::ok);
	SIM_EXPECT_EQ(fixture.Deconfigure(), ResultStatus::ok);
	SIM_EXPECT(!fixture.function.IsDataPathActive());
	SIM_EXPECT_EQ(events.stopped, 1);
	SIM_EXPECT(fixture.sim.FindEndpoint(0x81) == nullptr);
	SIM_EXPECT(fixture.sim.FindEndpoint(0x83) == nullptr);

	fixture.Enumerate();
	SIM_EXPECT_EQ(fixture.SelectAlternate(1), ResultStatus::ok);
	SIM_EXPECT_EQ(events.started, 2);
	ExpectNotification(fixture, SpeedChangeBytes);
	ExpectNotification(fixture, ConnectedBytes);

	// The frame of the old session was dropped: nothing is sent for it.
	fixture.device.Process();
	SIM_EXPECT(!fixture.sim.HasArmedTransfer(0x81));
	SIM_EXPECT_EQ(fixture.function.NextFrameLength(), 0);
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


// A bus reset in the middle of a session closes the bulk pair and the notification state, and the stop event runs. The next session opens
// the path again from alternate 0.
void TestBusResetInSession() {
	NcmFixture fixture;
	Events events;
	SetEvents(fixture, events);
	fixture.Configure();
	SIM_EXPECT_EQ(fixture.SelectAlternate(1), ResultStatus::ok);
	SIM_EXPECT(fixture.sim.HasArmedTransfer(0x02));
	SIM_EXPECT(fixture.sim.HasArmedTransfer(0x83));

	fixture.sim.InjectBusReset();
	SIM_EXPECT(!fixture.function.IsDataPathActive());
	SIM_EXPECT_EQ(events.stopped, 1);
	SIM_EXPECT(fixture.sim.FindEndpoint(0x02) == nullptr);
	SIM_EXPECT(fixture.sim.FindEndpoint(0x83) == nullptr);

	fixture.Enumerate();
	SIM_EXPECT_EQ(fixture.SelectAlternate(1), ResultStatus::ok);
	SIM_EXPECT(fixture.sim.HasArmedTransfer(0x02));
	ExpectNotification(fixture, SpeedChangeBytes);
	ExpectNotification(fixture, ConnectedBytes);
	SIM_EXPECT_EQ(events.started, 2);
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


// Alternate 1 then alternate 0 before the speed change has been taken. The host never saw the link come up, so no NETWORK_CONNECTION(0)
// follows the speed change: the connection pending from the open is dropped with the session.
void TestCloseBeforeLinkSeenSendsNoDisconnect() {
	NcmFixture fixture;
	fixture.Configure();
	SIM_EXPECT_EQ(fixture.SelectAlternate(1), ResultStatus::ok);
	SIM_EXPECT_EQ(fixture.SelectAlternate(0), ResultStatus::ok);

	ExpectNotification(fixture, SpeedChangeBytes);
	fixture.device.Process();
	SIM_EXPECT(!fixture.sim.HasArmedTransfer(0x83));
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


// The bulk speed change in flight when the endpoint halts: it is sent again after the clear, as it was never taken.
void TestSpeedChangeResentAfterHalt() {
	NcmFixture fixture;
	fixture.Configure();
	SIM_EXPECT_EQ(fixture.SelectAlternate(1), ResultStatus::ok);
	SIM_EXPECT(fixture.sim.HasArmedTransfer(0x83));

	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x02, 3, 0x0000, 0x0083, 0)), ResultStatus::ok);
	SIM_EXPECT(!fixture.sim.HasArmedTransfer(0x83));
	fixture.device.Process();
	SIM_EXPECT(!fixture.sim.HasArmedTransfer(0x83));

	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x02, 1, 0x0000, 0x0083, 0)), ResultStatus::ok);
	ExpectNotification(fixture, SpeedChangeBytes);
	ExpectNotification(fixture, ConnectedBytes);
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


// The connection notification in flight when the endpoint halts: it is sent again after the clear, once, and nothing else is sent.
void TestConnectionResentAfterHalt() {
	NcmFixture fixture;
	fixture.Configure();
	SIM_EXPECT_EQ(fixture.SelectAlternate(1), ResultStatus::ok);
	ExpectNotification(fixture, SpeedChangeBytes);
	SIM_EXPECT(fixture.sim.HasArmedTransfer(0x83));

	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x02, 3, 0x0000, 0x0083, 0)), ResultStatus::ok);
	SIM_EXPECT(!fixture.sim.HasArmedTransfer(0x83));
	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x02, 1, 0x0000, 0x0083, 0)), ResultStatus::ok);
	ExpectNotification(fixture, ConnectedBytes);
	fixture.device.Process();
	SIM_EXPECT(!fixture.sim.HasArmedTransfer(0x83));
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


// The link goes down and back up while the connection is in flight. The connection in flight is taken as sent, it says up, and the link is
// up again, so the host already holds the state the link is in: nothing follows it.
void TestLinkDownAndUpWhileInFlight() {
	NcmFixture fixture;
	fixture.Configure();
	SIM_EXPECT_EQ(fixture.SelectAlternate(1), ResultStatus::ok);
	ExpectNotification(fixture, SpeedChangeBytes);
	SIM_EXPECT(fixture.sim.HasArmedTransfer(0x83));

	fixture.function.SetLinkUp(false);
	fixture.function.SetLinkUp(true);
	ExpectNotification(fixture, ConnectedBytes);
	fixture.device.Process();
	SIM_EXPECT(!fixture.sim.HasArmedTransfer(0x83));
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


// The link goes up and down while the disconnect is in flight, from a link the host had seen down. The link ends down, the state the host
// takes from the disconnect, so nothing follows it.
void TestLinkUpAndDownWhileInFlight() {
	NcmFixture fixture;
	fixture.Configure();
	fixture.function.SetLinkUp(false);
	SIM_EXPECT_EQ(fixture.SelectAlternate(1), ResultStatus::ok);
	ExpectNotification(fixture, SpeedChangeBytes);
	SIM_EXPECT(fixture.sim.HasArmedTransfer(0x83));

	fixture.function.SetLinkUp(true);
	fixture.function.SetLinkUp(false);
	ExpectNotification(fixture, DisconnectedBytes);
	fixture.device.Process();
	SIM_EXPECT(!fixture.sim.HasArmedTransfer(0x83));
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


// The connection notification in flight when the endpoint halts, and the session closes before the halt clears. The host never took the
// connection, so it never had the link up: the close sends no disconnect, and the clear sends nothing.
void TestCloseAfterHaltedConnectionSendsNoDisconnect() {
	NcmFixture fixture;
	fixture.Configure();
	SIM_EXPECT_EQ(fixture.SelectAlternate(1), ResultStatus::ok);
	ExpectNotification(fixture, SpeedChangeBytes);
	SIM_EXPECT(fixture.sim.HasArmedTransfer(0x83));

	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x02, 3, 0x0000, 0x0083, 0)), ResultStatus::ok);
	SIM_EXPECT_EQ(fixture.SelectAlternate(0), ResultStatus::ok);
	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x02, 1, 0x0000, 0x0083, 0)), ResultStatus::ok);
	fixture.device.Process();
	SIM_EXPECT(!fixture.sim.HasArmedTransfer(0x83));
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


// The connection in flight when the session closes is taken by the host after the close. The host had the link up from that notification,
// so the disconnect follows it.
void TestCloseWithConnectionInFlightSendsDisconnectAfterIt() {
	NcmFixture fixture;
	fixture.Configure();
	SIM_EXPECT_EQ(fixture.SelectAlternate(1), ResultStatus::ok);
	ExpectNotification(fixture, SpeedChangeBytes);
	SIM_EXPECT(fixture.sim.HasArmedTransfer(0x83));

	SIM_EXPECT_EQ(fixture.SelectAlternate(0), ResultStatus::ok);
	ExpectNotification(fixture, ConnectedBytes);
	fixture.device.Process();
	ExpectNotification(fixture, DisconnectedBytes);
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


// SendFrame is refused while the path is closed (notReady), accepts four frames and refuses the fifth while the queue is full (busy).
void TestSendFrameStates() {
	NcmFixture fixture;
	fixture.Configure();
	SIM_EXPECT_EQ(fixture.function.SendFrame(HeaderFrame), ResultStatus::notReady);

	SIM_EXPECT_EQ(fixture.SelectAlternate(1), ResultStatus::ok);
	for (uint32 index = 0; index < 4; index++) {
		SIM_EXPECT_EQ(fixture.function.SendFrame(HeaderFrame), ResultStatus::ok);
	}

	SIM_EXPECT_EQ(fixture.function.SendFrame(HeaderFrame), ResultStatus::busy);

	// A frame outside 14 to 1514 bytes is refused whatever the state.
	std::array<uint8, 13> runt = {};
	SIM_EXPECT_EQ(fixture.function.SendFrame(runt), ResultStatus::invalidParameter);
	std::array<uint8, 1515> giant = {};
	SIM_EXPECT_EQ(fixture.function.SendFrame(giant), ResultStatus::invalidParameter);
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


// The handlers are refused once the device is configured, because the callables run from the device's context.
void TestHandlersBusyWhenConfigured() {
	NcmFixture fixture;
	fixture.Configure();
	Events events;
	SIM_EXPECT_EQ(fixture.function.SetDataPathStartedHandler(NcmTest::Function::DataPathDelegate::FromRaw(&CountStarted, &events)), ResultStatus::busy);
	SIM_EXPECT_EQ(fixture.function.SetDataPathStoppedHandler(NcmTest::Function::DataPathDelegate::FromRaw(&CountStopped, &events)), ResultStatus::busy);
}


}


int main() {
	TestAlternateOpensAndClosesEndpoints();
	TestOnlyTwoAlternates();
	TestGetInterface();
	TestNotificationOrderOnOpen();
	TestLinkDownAndUp();
	TestLinkToggleWithHostUpSendsNothing();
	TestLinkDownBeforeOpen();
	TestChangeWaitsForInFlight();
	TestDeconfigureEndsSession();
	TestBusResetInSession();
	TestCloseBeforeLinkSeenSendsNoDisconnect();
	TestSpeedChangeResentAfterHalt();
	TestConnectionResentAfterHalt();
	TestCloseAfterHaltedConnectionSendsNoDisconnect();
	TestCloseWithConnectionInFlightSendsDisconnectAfterIt();
	TestLinkDownAndUpWhileInFlight();
	TestLinkUpAndDownWhileInFlight();
	TestSendFrameStates();
	TestHandlersBusyWhenConfigured();
	return SimTest::Finish("TestNcmAlternates");
}
