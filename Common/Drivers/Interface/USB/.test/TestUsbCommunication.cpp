#include <VHAL.h>
#include "CdcTestDevices.h"
#include "SimAssert.h"
#include "SimHost.h"
#include <Drivers/Interface/USB/Class/CDC/USBCdcProtocol.h>
#include <Utilities/Bootloader/Communication/USBCommunication.h>
#include <array>
#include <cstddef>
#include <span>


// The bootloader's transport over the CDC-ACM function and over a vendor bulk function, driven through the simulated controller. The
// cases are the connection rules (the configuration, and DTR for CDC), the all-or-nothing SendData, the bytes both ways, the empty span
// on a disconnect and on a session change while connected, and the empty span not given when the transport was never connected. The
// reset from inside the data callback checks the order of the empty span and the new session's bytes, and that one empty span is given.
namespace {


// The bytes the transport delivered to its callback, and how many times it was called with an empty span.
struct Received {
	std::array<uint8, 256> bytes{};
	size_t count = 0;
	uint32 emptySpans = 0;
};


void Collect(Received& received, std::span<const uint8> data) {
	if (data.empty()) {
		received.emptySpans++;
		return;
	}

	for (uint8 value : data) {
		received.bytes[received.count] = value;
		received.count++;
	}
}


void TestCdcTransport() {
	CdcFixture fixture;
	USBCommunication<CdcTest::Function> transport(fixture.function);
	Received received;
	transport.SetDataReceivedCallback([&received](std::span<const uint8> data) {
		Collect(received, data);
	});

	// Before the device is configured the transport is down: nothing is sent and nothing is delivered.
	std::array<uint8, 10> packet = { 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19 };
	SIM_EXPECT(!transport.IsConnected());
	SIM_EXPECT_EQ(transport.SendData(packet), ResultStatus::notReady);
	transport.Process();
	SIM_EXPECT_EQ(received.emptySpans, 0);

	// Configured but DTR not set: the terminal is closed, so the transport is still down.
	fixture.Configure();
	SIM_EXPECT(!transport.IsConnected());
	SIM_EXPECT(!transport.IsReadyToSend());
	SIM_EXPECT_EQ(transport.SendData(packet), ResultStatus::notReady);

	// DTR set: connected. The packet goes out through the bulk IN endpoint.
	SIM_EXPECT_EQ(fixture.SetControlLines(USBCdcControlDtr), ResultStatus::ok);
	SIM_EXPECT(transport.IsConnected());
	SIM_EXPECT(transport.IsReadyToSend());
	SIM_EXPECT_EQ(transport.SendData(packet), ResultStatus::ok);
	fixture.device.Process();
	std::array<uint8, 64> in = {};
	uint32 inCount = 0;
	SIM_EXPECT_EQ(fixture.sim.HostIn(0x81, in, inCount), ResultStatus::ok);
	SIM_EXPECT_EQ(inCount, 10);
	SIM_EXPECT_BYTES(std::span<const uint8>(in).first(inCount), packet);

	// SendData is all or nothing: a packet that does not fit is refused whole and nothing of it is queued. A full ring is busy.
	std::array<uint8, 512> full = {};
	SIM_EXPECT_EQ(transport.SendData(full), ResultStatus::ok);
	SIM_EXPECT(!transport.IsReadyToSend());
	SIM_EXPECT_EQ(transport.SendData(std::span<const uint8>(full).first(1)), ResultStatus::busy);
	fixture.device.Process();
	std::array<uint8, 128> stage = {};
	SIM_EXPECT_EQ(fixture.sim.HostIn(0x81, stage, inCount), ResultStatus::ok);
	SIM_EXPECT_EQ(inCount, 128);
	SIM_EXPECT_EQ(transport.SendData(std::span<const uint8>(full).first(200)), ResultStatus::busy);
	SIM_EXPECT_EQ(transport.SendData(std::span<const uint8>(full).first(128)), ResultStatus::ok);

	// The host's bytes come to the callback, in order, from Process().
	std::array<uint8, 10> hostBytes = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
	SIM_EXPECT(fixture.sim.HostOut(0x02, hostBytes).IsOk());
	SIM_EXPECT(fixture.sim.HostOut(0x02, std::span<const uint8>()).IsOk());
	transport.Process();
	SIM_EXPECT_EQ(received.count, 10);
	for (size_t index = 0; index < hostBytes.size(); index++) {
		SIM_EXPECT_EQ(received.bytes[index], hostBytes[index]);
	}

	// A disconnect (SET_CONFIGURATION 0) gives the callback one empty span, and no more after it.
	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x00, 9, 0x0000, 0x0000, 0)), ResultStatus::ok);
	transport.Process();
	SIM_EXPECT_EQ(received.emptySpans, 1);
	transport.Process();
	SIM_EXPECT_EQ(received.emptySpans, 1);
	SIM_EXPECT(!transport.IsConnected());

	// Reconnected from nothing: the transport was down at the last Process(), so no empty span is given for the connection itself.
	fixture.Enumerate();
	SIM_EXPECT_EQ(fixture.SetControlLines(USBCdcControlDtr), ResultStatus::ok);
	transport.Process();
	SIM_EXPECT_EQ(received.emptySpans, 1);
	SIM_EXPECT(transport.IsConnected());

	// A session that changes between two Process() calls while the transport is connected: the framing is lost, so one empty span is
	// given. The session's own bytes are delivered after it.
	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x00, 9, 0x0000, 0x0000, 0)), ResultStatus::ok);
	fixture.Enumerate();
	SIM_EXPECT_EQ(fixture.SetControlLines(USBCdcControlDtr), ResultStatus::ok);
	SIM_EXPECT(transport.IsConnected());
	std::array<uint8, 1> last = { 0x77 };
	SIM_EXPECT(fixture.sim.HostOut(0x02, last).IsOk());
	SIM_EXPECT(fixture.sim.HostOut(0x02, std::span<const uint8>()).IsOk());
	transport.Process();
	SIM_EXPECT_EQ(received.emptySpans, 2);
	SIM_EXPECT_EQ(received.count, 11);
	SIM_EXPECT_EQ(received.bytes[10], 0x77);
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


// The bytes and empty spans the callback saw, in the order it saw them. An empty span is recorded as EmptySpanEvent.
constexpr uint16 EmptySpanEvent = 0x100;


struct Trace {
	std::array<uint16, 16> events{};
	size_t count = 0;
};


void Record(Trace& trace, std::span<const uint8> data) {
	if (data.empty()) {
		trace.events[trace.count] = EmptySpanEvent;
		trace.count++;
		return;
	}

	for (uint8 value : data) {
		trace.events[trace.count] = value;
		trace.count++;
	}
}


// The host sends one byte in the current session, then the zero-length packet that ends the transfer.
void HostSendsOneByte(CdcFixture& fixture, uint8 value) {
	std::array<uint8, 1> byte = { value };
	SIM_EXPECT(fixture.sim.HostOut(0x02, byte).IsOk());
	SIM_EXPECT(fixture.sim.HostOut(0x02, std::span<const uint8>()).IsOk());
}


// The bus is reset from inside the data callback, and the new session is set up there too. The host sends '2' in the new session.
// The old session's '1' is delivered first, then the empty span that ends the old session, then the new session's '2'. The bytes of
// the new session must not come before the empty span.
void TestResetInsideCallbackOrdersEmptySpan() {
	CdcFixture fixture;
	USBCommunication<CdcTest::Function> transport(fixture.function);
	fixture.Configure();
	SIM_EXPECT_EQ(fixture.SetControlLines(USBCdcControlDtr), ResultStatus::ok);
	SIM_EXPECT(transport.IsConnected());

	Trace trace;
	bool resetDone = false;
	transport.SetDataReceivedCallback([&fixture, &trace, &resetDone](std::span<const uint8> data) {
		Record(trace, data);
		if (resetDone) {
			return;
		}

		resetDone = true;
		fixture.Enumerate();
		SIM_EXPECT_EQ(fixture.SetControlLines(USBCdcControlDtr), ResultStatus::ok);
		HostSendsOneByte(fixture, 0x32);
	});

	HostSendsOneByte(fixture, 0x31);
	transport.Process();
	SIM_EXPECT_EQ(trace.count, 3);
	SIM_EXPECT_EQ(trace.events[0], 0x31);
	SIM_EXPECT_EQ(trace.events[1], EmptySpanEvent);
	SIM_EXPECT_EQ(trace.events[2], 0x32);

	// The session that ended gave its one empty span. Nothing more comes from the calls after it.
	transport.Process();
	SIM_EXPECT_EQ(trace.count, 3);
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


// The bus is reset from inside the data callback and nothing is sent after it. The transport gives one empty span, and later calls give
// no more.
void TestResetInsideCallbackGivesOneEmptySpan() {
	CdcFixture fixture;
	USBCommunication<CdcTest::Function> transport(fixture.function);
	fixture.Configure();
	SIM_EXPECT_EQ(fixture.SetControlLines(USBCdcControlDtr), ResultStatus::ok);

	Trace trace;
	bool resetDone = false;
	transport.SetDataReceivedCallback([&fixture, &trace, &resetDone](std::span<const uint8> data) {
		Record(trace, data);
		if (resetDone) {
			return;
		}

		resetDone = true;
		fixture.sim.InjectBusReset();
	});

	HostSendsOneByte(fixture, 0x31);
	transport.Process();
	SIM_EXPECT_EQ(trace.count, 2);
	SIM_EXPECT_EQ(trace.events[0], 0x31);
	SIM_EXPECT_EQ(trace.events[1], EmptySpanEvent);
	SIM_EXPECT(!transport.IsConnected());

	transport.Process();
	transport.Process();
	SIM_EXPECT_EQ(trace.count, 2);
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


// A bus reset clears DTR, so the transport is down after the device is configured again, until the host sets DTR. The session change is
// one empty span for a transport that was connected.
void TestBusResetDropsTransportUntilDtr() {
	CdcFixture fixture;
	USBCommunication<CdcTest::Function> transport(fixture.function);
	Received received;
	transport.SetDataReceivedCallback([&received](std::span<const uint8> data) {
		Collect(received, data);
	});

	fixture.Configure();
	SIM_EXPECT_EQ(fixture.SetControlLines(USBCdcControlDtr), ResultStatus::ok);
	SIM_EXPECT(transport.IsConnected());
	// The transport must see the connection in a Process() call, or it has no session to end.
	transport.Process();
	SIM_EXPECT_EQ(received.emptySpans, 0);

	fixture.Enumerate();
	SIM_EXPECT(fixture.function.IsConfigured());
	SIM_EXPECT(!transport.IsConnected());
	std::array<uint8, 4> packet = { 0x41, 0x42, 0x43, 0x44 };
	SIM_EXPECT_EQ(transport.SendData(packet), ResultStatus::notReady);

	transport.Process();
	SIM_EXPECT_EQ(received.emptySpans, 1);

	SIM_EXPECT_EQ(fixture.SetControlLines(USBCdcControlDtr), ResultStatus::ok);
	SIM_EXPECT(transport.IsConnected());
	SIM_EXPECT_EQ(transport.SendData(packet), ResultStatus::ok);
	transport.Process();
	SIM_EXPECT_EQ(received.emptySpans, 1);
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


// The transport over a vendor bulk function, which has no terminal signal: connected while configured.
void TestVendorTransport() {
	VendorFixtureOf<CdcTest::VendorUsb> fixture;
	USBCommunication<VendorBulkFunction<512, 256, 2>> transport(fixture.function);
	Received received;
	transport.SetDataReceivedCallback([&received](std::span<const uint8> data) {
		Collect(received, data);
	});

	SIM_EXPECT(!transport.IsConnected());
	fixture.Configure();
	SIM_EXPECT(transport.IsConnected());
	SIM_EXPECT_EQ(transport.GetMaxPacketSize(), 64);

	std::array<uint8, 6> packet = { 0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5 };
	SIM_EXPECT_EQ(transport.SendData(packet), ResultStatus::ok);
	fixture.device.Process();
	std::array<uint8, 64> in = {};
	uint32 inCount = 0;
	SIM_EXPECT_EQ(fixture.sim.HostIn(0x81, in, inCount), ResultStatus::ok);
	SIM_EXPECT_BYTES(std::span<const uint8>(in).first(inCount), packet);

	std::array<uint8, 3> hostBytes = { 0xB0, 0xB1, 0xB2 };
	SIM_EXPECT(fixture.sim.HostOut(0x02, hostBytes).IsOk());
	transport.Process();
	SIM_EXPECT_EQ(received.count, 3);
	SIM_EXPECT_EQ(received.bytes[2], 0xB2);

	// Deconfiguration is the disconnect: one empty span.
	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x00, 9, 0x0000, 0x0000, 0)), ResultStatus::ok);
	transport.Process();
	SIM_EXPECT_EQ(received.emptySpans, 1);
	SIM_EXPECT(!transport.IsConnected());
	SIM_EXPECT_EQ(transport.SendData(packet), ResultStatus::notReady);
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


}


int main() {
	TestCdcTransport();
	TestResetInsideCallbackOrdersEmptySpan();
	TestResetInsideCallbackGivesOneEmptySpan();
	TestBusResetDropsTransportUntilDtr();
	TestVendorTransport();
	return SimTest::Finish("TestUsbCommunication");
}
