#include <VHAL.h>
#include "CdcTestDevices.h"
#include "SimAssert.h"
#include "SimHost.h"
#include <Drivers/Interface/USB/Class/CDC/USBCdcProtocol.h>
#include <array>
#include <cstddef>
#include <span>


// The data interface of the CDC-ACM function: the bulk IN 0x81 and bulk OUT 0x02 of CdcTest::ExplicitUsb, through the same bulk engine
// as the vendor function. The cases are data both ways through the rings, the reset and the halt of each bulk endpoint, the
// deconfiguration (which drops DTR), and the notification endpoint kept apart from the bulk pair.
namespace {


// A byte pattern that is not a plain counter, so a chunk moved by a multiple of 256 shows up.
uint8 PatternByte(uint32 position) {
	return static_cast<uint8>((position * 131u) ^ (position >> 7));
}


// Writes count bytes of the pattern from position into the function's TX ring, and checks that all of them fit.
void WritePattern(CdcTest::Function& function, uint32 position, uint16 count) {
	std::array<uint8, 512> data = {};
	for (uint16 index = 0; index < count; index++) {
		data[index] = PatternByte(position + static_cast<uint32>(index));
	}

	SIM_EXPECT_EQ(function.Write(std::span<const uint8>(data).first(count)), count);
}


// Takes the armed IN transfer on 0x81 and checks it against the pattern from position. Returns the count received.
uint32 HostReadPattern(CdcFixture& fixture, uint32 position) {
	std::array<uint8, 128> in = {};
	uint32 received = 0;
	SIM_EXPECT_EQ(fixture.sim.HostIn(0x81, in, received), ResultStatus::ok);
	bool matches = true;
	for (uint32 index = 0; index < received; index++) {
		if (in[index] != PatternByte(position + index)) {
			matches = false;
		}
	}

	SIM_EXPECT(matches);
	return received;
}


// Stops and restarts an endpoint with SET_FEATURE and CLEAR_FEATURE (ENDPOINT_HALT) on the endpoint's address.
void HaltEndpoint(CdcFixture& fixture, uint16 address) {
	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x02, 3, 0x0000, address, 0)), ResultStatus::ok);
}


void ClearEndpointHalt(CdcFixture& fixture, uint16 address) {
	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x02, 1, 0x0000, address, 0)), ResultStatus::ok);
}


void TestBulkInDataBothWays() {
	CdcFixture fixture;
	fixture.Configure();
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);

	// Device to host: 64 bytes, a whole packet, so the transfer ends with a zero-length packet.
	WritePattern(fixture.function, 0, 64);
	fixture.device.Process();
	SIM_EXPECT_EQ(HostReadPattern(fixture, 0), 64);

	// Host to device: a full packet completes its one-packet transfer, and the zero-length packet after it is an empty transfer.
	std::array<uint8, 64> out = {};
	for (uint32 index = 0; index < out.size(); index++) {
		out[index] = PatternByte(index + 500);
	}

	SIM_EXPECT(fixture.sim.HostOut(0x02, out).IsOk());
	SIM_EXPECT(fixture.sim.HostOut(0x02, std::span<const uint8>()).IsOk());
	std::array<uint8, 64> read = {};
	SIM_EXPECT_EQ(fixture.function.Read(read), 64);
	for (uint32 index = 0; index < read.size(); index++) {
		SIM_EXPECT_EQ(read[index], out[index]);
	}

	SIM_EXPECT_EQ(fixture.function.GetReadableCount(), 0);
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


// Many transfers in order through the rings: 1000 bytes out and back, with the rings refilled as they drain.
void TestBulkStreamInOrder() {
	CdcFixture fixture;
	fixture.Configure();

	const uint32 total = 1000;
	uint32 written = 0;
	uint32 read = 0;
	for (uint32 round = 0; round < 100 && read < total; round++) {
		if (written < total) {
			uint32 chunk = total - written;
			if (chunk > fixture.function.GetWritableCount()) {
				chunk = fixture.function.GetWritableCount();
			}

			if (chunk > 0) {
				WritePattern(fixture.function, written, static_cast<uint16>(chunk));
				written += chunk;
			}
		}

		fixture.device.Process();
		uint32 guard = 0;
		while (fixture.sim.HasArmedTransfer(0x81) && guard < 64) {
			read += HostReadPattern(fixture, read);
			guard++;
			fixture.device.Process();
		}
	}

	SIM_EXPECT_EQ(written, total);
	SIM_EXPECT_EQ(read, total);
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


// A bus reset drops the bytes both rings hold, so the new session sends and reads only its own data.
void TestBusResetClearsRings() {
	CdcFixture fixture;
	fixture.Configure();

	WritePattern(fixture.function, 0, 256);
	fixture.device.Process();
	SIM_EXPECT(fixture.sim.HasArmedTransfer(0x81));

	fixture.Enumerate();
	SIM_EXPECT_EQ(fixture.function.GetWritableCount(), 512);
	std::array<uint8, 128> in = {};
	uint32 received = 0;
	SIM_EXPECT_EQ(fixture.sim.HostIn(0x81, in, received), ResultStatus::notReady);

	WritePattern(fixture.function, 1000, 64);
	fixture.device.Process();
	SIM_EXPECT_EQ(HostReadPattern(fixture, 1000), 64);
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


// A halt of the bulk IN endpoint sends nothing and keeps the bytes; the clear sends them from the start.
void TestBulkInHaltKeepsBytes() {
	CdcFixture fixture;
	fixture.Configure();

	WritePattern(fixture.function, 0, 128);
	fixture.device.Process();
	SIM_EXPECT(fixture.sim.HasArmedTransfer(0x81));

	HaltEndpoint(fixture, 0x0081);
	SIM_EXPECT(!fixture.sim.HasArmedTransfer(0x81));
	SIM_EXPECT_EQ(fixture.function.GetWritableCount(), 512 - 128);

	ClearEndpointHalt(fixture, 0x0081);
	fixture.device.Process();
	SIM_EXPECT_EQ(HostReadPattern(fixture, 0), 128);
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


// Two IN stages marked: the first completes and commits only its own bytes, the second starts at once. A halt then drops the second
// stage in flight, and its bytes go back to the ring, so they are sent after the clear. A commit that took every mark would lose them.
void TestBulkInHaltWithTwoStagesMarked() {
	CdcFixture fixture;
	fixture.Configure();

	WritePattern(fixture.function, 0, 256);
	fixture.device.Process();
	fixture.device.Process();
	SIM_EXPECT_EQ(fixture.function.GetWritableCount(), 256);

	SIM_EXPECT_EQ(HostReadPattern(fixture, 0), 128);
	SIM_EXPECT(fixture.sim.HasArmedTransfer(0x81));
	SIM_EXPECT_EQ(fixture.function.GetWritableCount(), 384);

	HaltEndpoint(fixture, 0x0081);
	SIM_EXPECT(!fixture.sim.HasArmedTransfer(0x81));
	SIM_EXPECT_EQ(fixture.function.GetWritableCount(), 384);

	ClearEndpointHalt(fixture, 0x0081);
	fixture.device.Process();
	SIM_EXPECT_EQ(HostReadPattern(fixture, 128), 128);
	SIM_EXPECT_EQ(fixture.function.GetWritableCount(), 512);
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


// A halt of the bulk OUT endpoint NAKs the host's data; after the clear, the host's packets are taken.
void TestBulkOutHaltNaksThenAccepts() {
	CdcFixture fixture;
	fixture.Configure();
	SIM_EXPECT(fixture.sim.HasArmedTransfer(0x02));

	HaltEndpoint(fixture, 0x0002);
	SIM_EXPECT(!fixture.sim.HasArmedTransfer(0x02));
	// A halted endpoint answers the host's packet with STALL, which the simulator reports as accessError.
	std::array<uint8, 64> data = {};
	SIM_EXPECT_EQ(fixture.sim.HostOut(0x02, data).Error(), ResultStatus::accessError);

	ClearEndpointHalt(fixture, 0x0002);
	SIM_EXPECT(fixture.sim.HasArmedTransfer(0x02));
	SIM_EXPECT(fixture.sim.HostOut(0x02, data).IsOk());
	SIM_EXPECT(fixture.sim.HostOut(0x02, std::span<const uint8>()).IsOk());
	SIM_EXPECT_EQ(fixture.function.GetReadableCount(), 64);
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


// SET_CONFIGURATION(0) drops the session, DTR and the bytes waiting to be sent: nothing is written or read while the device is not
// configured, and the terminal is closed. The next configuration starts from empty rings.
void TestDeconfigureDropsSession() {
	CdcFixture fixture;
	fixture.Configure();
	SIM_EXPECT_EQ(fixture.SetControlLines(USBCdcControlDtr), ResultStatus::ok);
	SIM_EXPECT(fixture.function.IsTerminalOpen());
	WritePattern(fixture.function, 0, 100);

	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x00, 9, 0, 0x0000, 0)), ResultStatus::ok);
	SIM_EXPECT(!fixture.sim.HasArmedTransfer(0x81));
	SIM_EXPECT(!fixture.sim.HasArmedTransfer(0x83));
	SIM_EXPECT(!fixture.function.IsTerminalOpen());
	SIM_EXPECT_EQ(fixture.function.GetWritableCount(), 0);
	std::array<uint8, 4> more = {};
	SIM_EXPECT_EQ(fixture.function.Write(more), 0);
	SIM_EXPECT_EQ(fixture.function.Read(std::span<uint8>()), 0);

	fixture.Enumerate();
	SIM_EXPECT_EQ(fixture.function.GetWritableCount(), 512);
	SIM_EXPECT_EQ(fixture.function.GetReadableCount(), 0);
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


// The notification endpoint's halt does not touch the bulk pair: the bulk IN still sends after it.
void TestNotificationHaltLeavesBulkAlone() {
	CdcFixture fixture;
	fixture.Configure();
	HaltEndpoint(fixture, 0x0083);

	WritePattern(fixture.function, 0, 64);
	fixture.device.Process();
	SIM_EXPECT(fixture.sim.HasArmedTransfer(0x81));
	SIM_EXPECT_EQ(HostReadPattern(fixture, 0), 64);
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


// The zero-length packet is seen through the DATA PID. The simulator flips it once per packet of a transfer and once more for a
// zero-length packet, so a transfer of two data packets (one stage of 128 bytes) leaves the PID flipped only when the zero-length
// packet follows, and a transfer of one packet (64 bytes) leaves it flipped only without one. The stage here is 128 bytes.

// A stage of exactly 128 bytes and nothing after it: the host's read is larger than the stage, and only a zero-length packet ends it.
void TestFullStageEndsWithZeroLengthPacket() {
	CdcFixture fixture;
	fixture.Configure();

	WritePattern(fixture.function, 0, 128);
	fixture.device.Process();
	bool toggleBefore = fixture.sim.GetDataToggle(0x81);
	SIM_EXPECT_EQ(HostReadPattern(fixture, 0), 128);
	SIM_EXPECT(fixture.sim.GetDataToggle(0x81) != toggleBefore);
	SIM_EXPECT(!fixture.sim.HasArmedTransfer(0x81));
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


// Two stages written at once: the first stage has bytes behind it, so it ends without a zero-length packet, and the second, with
// nothing behind it, ends with one. The first transfer leaves the PID as it was, and the second flips it.
void TestTwoStagesZeroLengthPacketOnlyAfterLast() {
	CdcFixture fixture;
	fixture.Configure();

	WritePattern(fixture.function, 0, 256);
	fixture.device.Process();
	fixture.device.Process();
	bool toggleBefore = fixture.sim.GetDataToggle(0x81);

	SIM_EXPECT_EQ(HostReadPattern(fixture, 0), 128);
	SIM_EXPECT(fixture.sim.GetDataToggle(0x81) == toggleBefore);
	SIM_EXPECT(fixture.sim.HasArmedTransfer(0x81));

	SIM_EXPECT_EQ(HostReadPattern(fixture, 128), 128);
	SIM_EXPECT(fixture.sim.GetDataToggle(0x81) != toggleBefore);
	SIM_EXPECT(!fixture.sim.HasArmedTransfer(0x81));
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


// A write of 64 bytes is a short stage of one packet, which ends with a zero-length packet as it always did: two flips, so the PID
// is as it was.
void TestPartialStageKeepsZeroLengthPacket() {
	CdcFixture fixture;
	fixture.Configure();

	WritePattern(fixture.function, 0, 64);
	fixture.device.Process();
	bool toggleBefore = fixture.sim.GetDataToggle(0x81);
	SIM_EXPECT_EQ(HostReadPattern(fixture, 0), 64);
	SIM_EXPECT(fixture.sim.GetDataToggle(0x81) == toggleBefore);
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


}


// A host write sent as its packets, one OUT transfer at a time, with no zero-length packet after it. The host sends the next packet
// once the endpoint is armed again, as a host does.
void HostWrite(CdcFixture& fixture, std::span<const uint8> data) {
	std::size_t sent = 0;
	while (sent < data.size()) {
		Result<uint32> stored = fixture.sim.HostOut(0x02, data.subspan(sent));
		SIM_EXPECT(stored.IsOk());
		if (!stored.IsOk() || stored.Value() == 0) {
			break;
		}

		sent += stored.Value();
	}

	SIM_EXPECT_EQ(sent, data.size());
}


// The bulk OUT transfer is one packet, so a write of exactly 64 bytes and one of 128 bytes, neither followed by a zero-length packet,
// reach Read() whole. Before the fix a whole-packet write waited in a multi-packet OUT transfer and never reached Read().
void TestWholePacketWritesReachRead() {
	CdcFixture fixture;
	fixture.Configure();

	std::array<uint8, 128> data = {};
	for (uint32 index = 0; index < data.size(); index++) {
		data[index] = PatternByte(index + 700);
	}

	std::array<uint8, 128> read = {};
	HostWrite(fixture, std::span<const uint8>(data).first(64));
	SIM_EXPECT_EQ(fixture.function.GetReadableCount(), 64);
	SIM_EXPECT(fixture.sim.HasArmedTransfer(0x02));
	SIM_EXPECT_EQ(fixture.function.Read(read), 64);
	SIM_EXPECT(SimHost::SameBytes(std::span<const uint8>(read).first(64), std::span<const uint8>(data).first(64)));

	HostWrite(fixture, data);
	SIM_EXPECT_EQ(fixture.function.GetReadableCount(), 128);
	SIM_EXPECT(fixture.sim.HasArmedTransfer(0x02));
	SIM_EXPECT_EQ(fixture.function.Read(read), 128);
	SIM_EXPECT(SimHost::SameBytes(std::span<const uint8>(read), std::span<const uint8>(data)));
	SIM_EXPECT_EQ(fixture.function.GetReadableCount(), 0);
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


int main() {
	TestWholePacketWritesReachRead();
	TestBulkInDataBothWays();
	TestBulkStreamInOrder();
	TestBusResetClearsRings();
	TestBulkInHaltKeepsBytes();
	TestBulkInHaltWithTwoStagesMarked();
	TestBulkOutHaltNaksThenAccepts();
	TestDeconfigureDropsSession();
	TestNotificationHaltLeavesBulkAlone();
	TestFullStageEndsWithZeroLengthPacket();
	TestTwoStagesZeroLengthPacketOnlyAfterLast();
	TestPartialStageKeepsZeroLengthPacket();
	return SimTest::Finish("TestCdcAcmData");
}
