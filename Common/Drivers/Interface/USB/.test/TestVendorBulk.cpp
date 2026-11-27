#include <VHAL.h>
#include "Descriptors.h"
#include "SimAssert.h"
#include "SimHost.h"
#include "SimUSBDeviceAdapter.h"
#include <Drivers/Interface/USB/Class/Vendor/VendorBulkFunction.h>
#include <Drivers/Interface/USB/Class/Vendor/VendorBulkStreamFunction.h>
#include <Drivers/Interface/USB/Device/USBDevice.h>
#include <array>
#include <cstddef>
#include <functional>
#include <span>

// The vendor bulk function in buffered mode, run through the simulated controller with exact bytes. The rings are small so
// the limits are reached with few bytes: the TX ring is 100 bytes, the RX ring is 256, an IN transfer is 2 packets (128 bytes),
// and an OUT transfer is one packet (64 bytes). The cases are a full OUT FIFO, an empty IN FIFO, a wrap-around, a transfer that
// ends on a 64-byte boundary, a zero-length packet, and host writes of whole packets that reach Read() without one.
//
// IN has two stages (InStageCount). The cases from TestStagesBackToBack on use CountingFunction, a 512-byte TX ring whose
// ProduceIn() is counted, so a case sees which side filled each stage: Process() or the completion interrupt. A hook runs
// inside the thread's fill, after its marks are made, which is where an interrupt lands while the claim is held. The
// stream case checks that the producer runs from Process(), and that a stream still runs without Process() after the first
// transfer.


namespace {


constexpr uint8 Interfaces[] = {
	0
};


// 0x01 shares endpoint number 1 with 0x81 on fsdev, so this pair cannot be double-buffered there.
constexpr uint8 Endpoints[] = {
	0x81,
	0x01
};


// 0x02 is a separate number, so this pair can be double-buffered on a port that supports it.
constexpr uint8 SeparateNumberEndpoints[] = {
	0x81,
	0x02
};


using VendorFunction = VendorBulkFunction<100, 256, 2>;


// The buffered function with a 512-byte TX ring, so four 128-byte IN transfers fit in it. ProduceIn() is counted. afterProduce
// runs once, after the next ProduceIn() has returned: the thread's fill has made its marks and the claim is still held.
class CountingFunction : public VendorBulkFunction<512, 256, 2> {
public:
	uint32 produceCount = 0;
	std::function<void()> afterProduce;
	std::function<void()> beforeCommit;


	CountingFunction(std::span<const uint8> interfaces, std::span<const uint8> endpoints)
		: VendorBulkFunction<512, 256, 2>(interfaces, endpoints) {
	}


protected:
	size_t ProduceIn(std::span<uint8> packets) override {
		produceCount++;
		size_t count = VendorBulkFunction<512, 256, 2>::ProduceIn(packets);
		if (afterProduce != nullptr) {
			std::function<void()> hook = afterProduce;
			afterProduce = nullptr;
			hook();
		}

		return count;
	}


	// beforeCommit runs once, at the start of the next CommitIn(), before the ring commit. A bus reset that lands there lands
	// inside the thread's commit, with the claim held.
	void CommitIn(size_t count) override {
		if (beforeCommit != nullptr) {
			std::function<void()> hook = beforeCommit;
			beforeCommit = nullptr;
			hook();
		}

		VendorBulkFunction<512, 256, 2>::CommitIn(count);
	}
};


using StreamFunction = VendorBulkStreamFunction<2>;


template<typename TFunction>
struct FixtureOf {
	SimUSBDeviceAdapter sim;
	TFunction function;
	std::array<USBFunction*, 1> functions;
	USBDevice device;


	explicit FixtureOf(std::span<const uint8> endpoints = Endpoints, const USBDescriptorSet& descriptors = SimDescriptors::SimpleSet)
		: function(Interfaces, endpoints),
		  functions{ &function },
		  device(sim, descriptors, functions) {
	}


	// Attaches the device and configures it, as a host does.
	void Configure() {
		SIM_EXPECT_EQ(device.Start(USBDeviceAdapter::Parameters()), ResultStatus::ok);
		Enumerate();
	}


	// Resets the bus and configures the device again, as a host does after a reset.
	void Enumerate() {
		sim.InjectBusReset();
		SIM_EXPECT_EQ(SimHost::Control(sim, SimHost::MakeSetup(0x00, 5, 5, 0x0000, 0)), ResultStatus::ok);
		SIM_EXPECT_EQ(SimHost::Control(sim, SimHost::MakeSetup(0x00, 9, 1, 0x0000, 0)), ResultStatus::ok);
		SIM_EXPECT_EQ(device.GetState(), USBDeviceState::Configured);
	}
};


using VendorFixture = FixtureOf<VendorFunction>;
using CountingFixture = FixtureOf<CountingFunction>;
using StreamFixture = FixtureOf<StreamFunction>;


// Every byte is its position, so a byte that is lost, duplicated or moved shows up in the comparison.
void FillPattern(std::span<uint8> data) {
	for (std::size_t index = 0; index < data.size(); index++) {
		data[index] = static_cast<uint8>(index);
	}
}


bool SameBytes(std::span<const uint8> actual, std::span<const uint8> expected) {
	return SimHost::SameBytes(actual, expected);
}


// The byte at a stream position. It is not a plain counter, so a chunk moved by a multiple of 256 still shows up.
uint8 PatternByte(uint32 position) {
	return static_cast<uint8>((position * 131u) ^ (position >> 7));
}


bool MatchesPattern(std::span<const uint8> data, uint32 position) {
	for (std::size_t index = 0; index < data.size(); index++) {
		if (data[index] != PatternByte(position + static_cast<uint32>(index))) {
			return false;
		}
	}

	return true;
}


// Writes count bytes of the pattern from position into the function's TX ring.
template<typename TFunction>
uint16 WritePattern(TFunction& function, uint32 position, uint16 count) {
	std::array<uint8, 512> data = {};
	for (uint16 index = 0; index < count; index++) {
		data[index] = PatternByte(position + static_cast<uint32>(index));
	}

	return function.Write(std::span<const uint8>(data).first(count));
}


// Takes the armed IN transfer and checks its bytes against the pattern from position. Returns the count received.
template<typename TFixture>
uint32 HostReadPattern(TFixture& fixture, uint32 position) {
	std::array<uint8, 128> in = {};
	uint32 received = 0;
	SIM_EXPECT_EQ(fixture.sim.HostIn(0x81, in, received), ResultStatus::ok);
	SIM_EXPECT(MatchesPattern(std::span<const uint8>(in).first(received), position));
	return received;
}


// Stops the IN endpoint with SET_FEATURE(ENDPOINT_HALT), as the host does.
template<typename TFixture>
void HaltIn(TFixture& fixture) {
	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x02, 3, 0x0000, 0x0081, 0)), ResultStatus::ok);
}


// Clears the halt with CLEAR_FEATURE(ENDPOINT_HALT), as the host does.
template<typename TFixture>
void ClearInHalt(TFixture& fixture) {
	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x02, 1, 0x0000, 0x0081, 0)), ResultStatus::ok);
}


void TestInFifoEmptyNaks() {
	VendorFixture fixture;
	fixture.Configure();
	fixture.device.Process();

	// Nothing is queued: the IN endpoint is not armed, and the host gets NAK, not a zero-length packet.
	SIM_EXPECT(!fixture.sim.HasArmedTransfer(0x81));
	std::array<uint8, 128> in = {};
	uint32 received = 0;
	SIM_EXPECT_EQ(fixture.sim.HostIn(0x81, in, received), ResultStatus::notReady);
	SIM_EXPECT_EQ(received, 0);
	SIM_EXPECT_EQ(fixture.function.GetWritableCount(), 100);
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


void TestInFifoFullAndWrap() {
	VendorFixture fixture;
	fixture.Configure();

	std::array<uint8, 256> pattern = {};
	FillPattern(pattern);
	std::array<uint8, 256> in = {};
	uint32 received = 0;

	// 60 bytes go out, so the read index moves to 60 once the transfer completes.
	SIM_EXPECT_EQ(fixture.function.Write(std::span<const uint8>(pattern).first(60)), 60);
	fixture.device.Process();
	SIM_EXPECT_EQ(fixture.sim.HostIn(0x81, in, received), ResultStatus::ok);
	SIM_EXPECT(SameBytes(std::span<const uint8>(in).first(received), std::span<const uint8>(pattern).subspan(0, 60)));

	// 70 more bytes sit in the ring from index 60, so they wrap past the end of the 100-byte ring.
	SIM_EXPECT_EQ(fixture.function.Write(std::span<const uint8>(pattern).subspan(60, 70)), 70);
	fixture.device.Process();
	SIM_EXPECT_EQ(fixture.sim.HostIn(0x81, in, received), ResultStatus::ok);
	SIM_EXPECT_EQ(received, 70);
	SIM_EXPECT(SameBytes(std::span<const uint8>(in).first(received), std::span<const uint8>(pattern).subspan(60, 70)));

	// A write larger than the free space takes only what fits. The ring is then full, so the next write takes nothing.
	SIM_EXPECT_EQ(fixture.function.Write(std::span<const uint8>(pattern).subspan(130, 120)), 100);
	SIM_EXPECT_EQ(fixture.function.GetWritableCount(), 0);
	SIM_EXPECT_EQ(fixture.function.Write(std::span<const uint8>(pattern).subspan(230, 10)), 0);
	fixture.device.Process();
	SIM_EXPECT_EQ(fixture.sim.HostIn(0x81, in, received), ResultStatus::ok);
	SIM_EXPECT_EQ(received, 100);
	SIM_EXPECT(SameBytes(std::span<const uint8>(in).first(received), std::span<const uint8>(pattern).subspan(130, 100)));

	// Everything was read, so the FIFO is empty again and the endpoint NAKs.
	fixture.device.Process();
	SIM_EXPECT(!fixture.sim.HasArmedTransfer(0x81));
	SIM_EXPECT_EQ(fixture.function.GetWritableCount(), 100);
}


// A 64-byte IN transfer ends with a zero-length packet, so the host sees the end of the transfer. The packet count shows
// in the toggle: one data packet and the zero-length packet bring DATA0 back.
void TestInTransferEndsOnPacketBoundary() {
	VendorFixture fixture;
	fixture.Configure();

	std::array<uint8, 64> pattern = {};
	FillPattern(pattern);
	SIM_EXPECT_EQ(fixture.function.Write(pattern), 64);
	fixture.device.Process();
	SIM_EXPECT(fixture.sim.HasArmedTransfer(0x81));

	std::array<uint8, 64> in = {};
	uint32 received = 0;
	SIM_EXPECT_EQ(fixture.sim.HostIn(0x81, in, received), ResultStatus::ok);
	SIM_EXPECT_EQ(received, 64);
	SIM_EXPECT(SameBytes(std::span<const uint8>(in), std::span<const uint8>(pattern)));
	SIM_EXPECT(!fixture.sim.HasArmedTransfer(0x81));
	SIM_EXPECT(!fixture.sim.GetDataToggle(0x81));
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


void TestOutFifoFullAndNak() {
	VendorFixture fixture;
	fixture.Configure();

	std::array<uint8, 300> data = {};
	FillPattern(data);
	std::array<uint8, 300> read = {};
	SIM_EXPECT(fixture.sim.HasArmedTransfer(0x01));

	// Each OUT transfer is one packet, so each host packet completes its own transfer and the function stores it at once. Four
	// packets of 64 bytes fill the RX ring to 256. HostOut() takes one packet, the one that completes the transfer, and leaves the
	// rest of the data to the host.
	Result<uint32> stored = fixture.sim.HostOut(0x01, std::span<const uint8>(data).first(256));
	SIM_EXPECT(stored.IsOk());
	SIM_EXPECT_EQ(stored.Value(), 64);
	for (std::size_t packet = 1; packet < 4; packet++) {
		stored = fixture.sim.HostOut(0x01, std::span<const uint8>(data).subspan(packet * 64, 64));
		SIM_EXPECT(stored.IsOk());
		SIM_EXPECT_EQ(stored.Value(), 64);
	}

	SIM_EXPECT_EQ(fixture.function.GetReadableCount(), 256);

	// The RX ring is full, so the next transfer is not armed. The host's packets are NAKed, and nothing is taken.
	SIM_EXPECT(!fixture.sim.HasArmedTransfer(0x01));
	SIM_EXPECT_EQ(fixture.sim.HostOut(0x01, std::span<const uint8>(data).subspan(256, 44)).Error(), ResultStatus::notReady);
	SIM_EXPECT_EQ(fixture.function.GetReadableCount(), 256);

	// Reading frees the room. The bytes come out in the order they went in, and the endpoint is armed again.
	uint16 count = fixture.function.Read(read);
	SIM_EXPECT_EQ(count, 256);
	SIM_EXPECT(SameBytes(std::span<const uint8>(read).first(count), std::span<const uint8>(data).first(256)));
	fixture.device.Process();
	SIM_EXPECT(fixture.sim.HasArmedTransfer(0x01));

	// The NAKed bytes now go through, in a short packet that completes the transfer.
	stored = fixture.sim.HostOut(0x01, std::span<const uint8>(data).subspan(256, 44));
	SIM_EXPECT(stored.IsOk());
	SIM_EXPECT_EQ(stored.Value(), 44);
	count = fixture.function.Read(read);
	SIM_EXPECT_EQ(count, 44);
	SIM_EXPECT(SameBytes(std::span<const uint8>(read).first(count), std::span<const uint8>(data).subspan(256, 44)));
	SIM_EXPECT_EQ(fixture.function.GetReadableCount(), 0);
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


// A zero-length OUT packet on its own completes an empty transfer, and the function re-arms without taking any data. A full packet
// completes its one-packet transfer at once, so the 64 bytes are stored with no zero-length packet after them. The zero-length
// packet that follows is then an empty transfer of its own.
void TestOutZeroLengthPacketAndBoundary() {
	VendorFixture fixture;
	fixture.Configure();

	std::array<uint8, 64> data = {};
	FillPattern(data);
	std::array<uint8, 64> read = {};

	Result<uint32> stored = fixture.sim.HostOut(0x01, std::span<const uint8>());
	SIM_EXPECT(stored.IsOk());
	SIM_EXPECT_EQ(stored.Value(), 0);
	SIM_EXPECT_EQ(fixture.function.Read(read), 0);
	SIM_EXPECT(fixture.sim.HasArmedTransfer(0x01));

	// One full packet completes the transfer, so the 64 bytes are stored and the endpoint is armed again.
	stored = fixture.sim.HostOut(0x01, data);
	SIM_EXPECT(stored.IsOk());
	SIM_EXPECT_EQ(stored.Value(), 64);
	SIM_EXPECT(fixture.sim.HasArmedTransfer(0x01));
	// The zero-length packet after it is an empty transfer, so it stores nothing: 0 bytes, not 64.
	stored = fixture.sim.HostOut(0x01, std::span<const uint8>());
	SIM_EXPECT(stored.IsOk());
	SIM_EXPECT_EQ(stored.Value(), 0);
	SIM_EXPECT(fixture.sim.HasArmedTransfer(0x01));
	uint16 count = fixture.function.Read(read);
	SIM_EXPECT_EQ(count, 64);
	SIM_EXPECT(SameBytes(std::span<const uint8>(read).first(count), std::span<const uint8>(data)));
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


// Configures the function on an endpoint pair and checks the buffering the port granted, then moves data both ways. The OUT
// transfer is one packet, so the full packet completes it with 64 bytes, and the zero-length packet after it is an empty transfer.
void TestEndpointPair(std::span<const uint8> endpoints, uint8 outAddress, bool doubleBuffered) {
	VendorFixture fixture(endpoints);
	fixture.Configure();
	SIM_EXPECT_EQ(fixture.sim.IsDoubleBuffered(0x81), doubleBuffered);
	SIM_EXPECT_EQ(fixture.sim.IsDoubleBuffered(outAddress), doubleBuffered);
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);

	std::array<uint8, 64> pattern = {};
	FillPattern(pattern);

	SIM_EXPECT_EQ(fixture.function.Write(pattern), 64);
	fixture.device.Process();
	std::array<uint8, 64> in = {};
	uint32 received = 0;
	SIM_EXPECT_EQ(fixture.sim.HostIn(0x81, in, received), ResultStatus::ok);
	SIM_EXPECT_EQ(received, 64);
	SIM_EXPECT(SameBytes(in, pattern));

	Result<uint32> stored = fixture.sim.HostOut(outAddress, pattern);
	SIM_EXPECT(stored.IsOk());
	SIM_EXPECT_EQ(stored.Value(), 64);
	stored = fixture.sim.HostOut(outAddress, std::span<const uint8>());
	SIM_EXPECT(stored.IsOk());
	SIM_EXPECT_EQ(stored.Value(), 0);

	std::array<uint8, 64> read = {};
	SIM_EXPECT_EQ(fixture.function.Read(read), 64);
	SIM_EXPECT(SameBytes(read, pattern));
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


// The 0x81 and 0x02 pair gets double buffering on a port that supports it. The descriptor set (SimDescriptors::SeparateNumberSet) advertises
// 0x81 and 0x02, so the core sizes the buffer table for both numbers, and the function's OUT on 0x02 is allowed. The OUT itself is not driven
// here: IN on 0x81 is the transfer this test checks.
void TestSeparateNumberPairBuffering() {
	VendorFixture fixture(SeparateNumberEndpoints, SimDescriptors::SeparateNumberSet);
	fixture.Configure();
	SIM_EXPECT_EQ(fixture.sim.IsDoubleBuffered(0x81), true);
	SIM_EXPECT_EQ(fixture.sim.IsDoubleBuffered(0x02), true);
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);

	std::array<uint8, 64> pattern = {};
	FillPattern(pattern);
	SIM_EXPECT_EQ(fixture.function.Write(pattern), 64);
	fixture.device.Process();
	std::array<uint8, 64> in = {};
	uint32 received = 0;
	SIM_EXPECT_EQ(fixture.sim.HostIn(0x81, in, received), ResultStatus::ok);
	SIM_EXPECT_EQ(received, 64);
	SIM_EXPECT(SameBytes(in, pattern));
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


// Two IN stages in turn: Process() fills the second stage while the first is in flight, and the completion interrupt starts it
// without producing anything. Process() fills the freed stage next, so the transfers go back to back with no interrupt-side fill.
void TestStagesBackToBack() {
	CountingFixture fixture;
	fixture.Configure();
	SIM_EXPECT_EQ(WritePattern(fixture.function, 0, 512), 512);

	// The thread fills the first stage and starts it, then fills the second while the first is in flight.
	fixture.device.Process();
	SIM_EXPECT(fixture.sim.HasArmedTransfer(0x81));
	SIM_EXPECT_EQ(fixture.function.produceCount, 1);
	fixture.device.Process();
	SIM_EXPECT_EQ(fixture.function.produceCount, 2);

	// Both stages are filled, so a further Process() fills nothing.
	fixture.device.Process();
	SIM_EXPECT_EQ(fixture.function.produceCount, 2);

	// The first transfer completes. The interrupt starts the filled second stage and produces nothing. Only the first stage's
	// 128 bytes are committed: the ring still holds the second stage's 128 bytes and the 256 not yet staged, 384 in all.
	SIM_EXPECT_EQ(HostReadPattern(fixture, 0), 128);
	SIM_EXPECT_EQ(fixture.function.produceCount, 2);
	SIM_EXPECT(fixture.sim.HasArmedTransfer(0x81));
	SIM_EXPECT_EQ(fixture.function.GetWritableCount(), 128);

	fixture.device.Process();
	SIM_EXPECT_EQ(fixture.function.produceCount, 3);
	SIM_EXPECT_EQ(HostReadPattern(fixture, 128), 128);
	SIM_EXPECT_EQ(fixture.function.produceCount, 3);
	SIM_EXPECT(fixture.sim.HasArmedTransfer(0x81));

	fixture.device.Process();
	SIM_EXPECT_EQ(fixture.function.produceCount, 4);
	SIM_EXPECT_EQ(HostReadPattern(fixture, 256), 128);
	SIM_EXPECT_EQ(fixture.function.produceCount, 4);
	SIM_EXPECT(fixture.sim.HasArmedTransfer(0x81));

	// The last stage completes with nothing staged, so the interrupt finds the ring empty and the endpoint NAKs.
	SIM_EXPECT_EQ(HostReadPattern(fixture, 384), 128);
	SIM_EXPECT_EQ(fixture.function.produceCount, 5);
	SIM_EXPECT(!fixture.sim.HasArmedTransfer(0x81));
	SIM_EXPECT_EQ(fixture.function.GetWritableCount(), 512);
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


// A short transfer that is staged behind a full one still ends with a zero-length packet, as a transfer alone does.
void TestStagedShortTransferEndsWithZeroLengthPacket() {
	CountingFixture fixture;
	fixture.Configure();
	SIM_EXPECT_EQ(WritePattern(fixture.function, 0, 192), 192);

	fixture.device.Process();
	fixture.device.Process();
	SIM_EXPECT_EQ(HostReadPattern(fixture, 0), 128);

	// The second stage holds 64 bytes, a whole packet. Its transfer ends with a zero-length packet, which toggles the data PID
	// once more, so the toggle is the same after the transfer as it was when it started.
	bool toggleBefore = fixture.sim.GetDataToggle(0x81);
	SIM_EXPECT_EQ(HostReadPattern(fixture, 128), 64);
	SIM_EXPECT_EQ(fixture.sim.GetDataToggle(0x81), toggleBefore);
	SIM_EXPECT(!fixture.sim.HasArmedTransfer(0x81));
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


// Nothing is staged while Process() never runs again: each completion produces the next stage in the interrupt, as the
// single-stage base did, so the transfers keep coming.
void TestInterruptFallbackOnly() {
	CountingFixture fixture;
	fixture.Configure();
	SIM_EXPECT_EQ(WritePattern(fixture.function, 0, 512), 512);

	fixture.device.Process();
	SIM_EXPECT_EQ(fixture.function.produceCount, 1);
	SIM_EXPECT_EQ(HostReadPattern(fixture, 0), 128);
	SIM_EXPECT_EQ(fixture.function.produceCount, 2);
	SIM_EXPECT(fixture.sim.HasArmedTransfer(0x81));
	SIM_EXPECT_EQ(HostReadPattern(fixture, 128), 128);
	SIM_EXPECT_EQ(fixture.function.produceCount, 3);
	SIM_EXPECT_EQ(HostReadPattern(fixture, 256), 128);
	SIM_EXPECT_EQ(fixture.function.produceCount, 4);
	SIM_EXPECT_EQ(HostReadPattern(fixture, 384), 128);
	SIM_EXPECT_EQ(fixture.function.produceCount, 5);
	SIM_EXPECT(!fixture.sim.HasArmedTransfer(0x81));
	SIM_EXPECT_EQ(fixture.function.GetWritableCount(), 512);
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


// A halt with one stage in flight and one staged. Both stages were unsent, so all 256 bytes come back to the ring, in order,
// and the clear sends them again from the start.
void TestHaltWithOneInFlightAndOneStaged() {
	CountingFixture fixture;
	fixture.Configure();
	SIM_EXPECT_EQ(WritePattern(fixture.function, 0, 256), 256);

	fixture.device.Process();
	fixture.device.Process();
	SIM_EXPECT_EQ(fixture.function.produceCount, 2);
	SIM_EXPECT_EQ(fixture.function.GetWritableCount(), 256);

	HaltIn(fixture);
	SIM_EXPECT(!fixture.sim.HasArmedTransfer(0x81));
	SIM_EXPECT_EQ(fixture.function.GetWritableCount(), 256);

	// The clear finds nothing staged, so the interrupt produces the first stage again.
	ClearInHalt(fixture);
	SIM_EXPECT(fixture.sim.HasArmedTransfer(0x81));
	SIM_EXPECT_EQ(fixture.function.produceCount, 3);
	SIM_EXPECT_EQ(HostReadPattern(fixture, 0), 128);
	SIM_EXPECT_EQ(HostReadPattern(fixture, 128), 128);
	SIM_EXPECT(!fixture.sim.HasArmedTransfer(0x81));
	SIM_EXPECT_EQ(fixture.function.GetWritableCount(), 512);
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


// A bus reset with one stage in flight and one staged. The reset clears the ring, so no byte of the old session is sent after the
// host configures the device again. The new session sends only its own bytes.
void TestBusResetWithOneInFlightAndOneStaged() {
	CountingFixture fixture;
	fixture.Configure();
	SIM_EXPECT_EQ(WritePattern(fixture.function, 0, 256), 256);

	fixture.device.Process();
	fixture.device.Process();
	SIM_EXPECT(fixture.sim.HasArmedTransfer(0x81));

	fixture.Enumerate();
	SIM_EXPECT_EQ(fixture.function.GetWritableCount(), 512);

	// The host's IN token finds nothing: the old session's bytes are gone.
	std::array<uint8, 128> in = {};
	uint32 received = 0;
	SIM_EXPECT_EQ(fixture.sim.HostIn(0x81, in, received), ResultStatus::notReady);
	fixture.device.Process();
	SIM_EXPECT(!fixture.sim.HasArmedTransfer(0x81));

	SIM_EXPECT_EQ(WritePattern(fixture.function, 1000, 128), 128);
	fixture.device.Process();
	SIM_EXPECT_EQ(HostReadPattern(fixture, 1000), 128);
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


// Deconfiguration and configuration with stages in flight. The endpoints close, the stages are forgotten, and the ring is
// cleared when the device is configured again, so the old bytes are not sent.
void TestReconfigurationWithStagesInFlight() {
	CountingFixture fixture;
	fixture.Configure();
	SIM_EXPECT_EQ(WritePattern(fixture.function, 0, 256), 256);

	fixture.device.Process();
	fixture.device.Process();
	SIM_EXPECT(fixture.sim.HasArmedTransfer(0x81));

	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x00, 9, 0, 0x0000, 0)), ResultStatus::ok);
	SIM_EXPECT(!fixture.sim.HasArmedTransfer(0x81));
	SIM_EXPECT_EQ(fixture.function.GetWritableCount(), 0);

	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x00, 9, 1, 0x0000, 0)), ResultStatus::ok);
	SIM_EXPECT_EQ(fixture.function.GetWritableCount(), 512);
	fixture.device.Process();
	SIM_EXPECT(!fixture.sim.HasArmedTransfer(0x81));

	SIM_EXPECT_EQ(WritePattern(fixture.function, 2000, 128), 128);
	fixture.device.Process();
	SIM_EXPECT_EQ(HostReadPattern(fixture, 2000), 128);
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


// The first stage completes while the thread fills the second. The interrupt only marks the completion. Process() commits the
// first stage when it leaves its claim, and then starts the second, so its bytes come out in order and nothing is lost.
void TestCompletionDuringFill() {
	CountingFixture fixture;
	fixture.Configure();
	SIM_EXPECT_EQ(WritePattern(fixture.function, 0, 384), 384);

	fixture.device.Process();
	SIM_EXPECT_EQ(fixture.function.produceCount, 1);

	// The host takes the first transfer during the thread's fill of the second stage.
	std::array<uint8, 128> in = {};
	fixture.function.afterProduce = [&fixture, &in]() {
		uint32 received = 0;
		SIM_EXPECT_EQ(fixture.sim.HostIn(0x81, in, received), ResultStatus::ok);
		SIM_EXPECT_EQ(received, 128);
		SIM_EXPECT(MatchesPattern(std::span<const uint8>(in).first(received), 0));
	};

	fixture.device.Process();
	SIM_EXPECT_EQ(fixture.function.produceCount, 2);
	SIM_EXPECT(fixture.function.afterProduce == nullptr);

	// The second stage was started when the thread left its claim, and the first stage's 128 bytes are committed.
	SIM_EXPECT(fixture.sim.HasArmedTransfer(0x81));
	SIM_EXPECT_EQ(fixture.function.GetWritableCount(), 256);
	SIM_EXPECT_EQ(HostReadPattern(fixture, 128), 128);
	SIM_EXPECT_EQ(fixture.function.produceCount, 3);
	SIM_EXPECT_EQ(HostReadPattern(fixture, 256), 128);
	SIM_EXPECT(!fixture.sim.HasArmedTransfer(0x81));
	SIM_EXPECT_EQ(fixture.function.GetWritableCount(), 512);
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


// The endpoint halts while the thread fills the second stage. The halt only sets a pending flag, and Process() rolls both stages
// back when it leaves its claim. Nothing was sent, so all 384 bytes come back, and the clear sends them from the start.
void TestHaltDuringFill() {
	CountingFixture fixture;
	fixture.Configure();
	SIM_EXPECT_EQ(WritePattern(fixture.function, 0, 384), 384);

	fixture.device.Process();
	SIM_EXPECT_EQ(fixture.function.produceCount, 1);

	fixture.function.afterProduce = [&fixture]() {
		HaltIn(fixture);
	};

	fixture.device.Process();
	SIM_EXPECT(fixture.function.afterProduce == nullptr);
	SIM_EXPECT(!fixture.sim.HasArmedTransfer(0x81));
	// Nothing was committed, so the ring still holds all 384 bytes, and 128 of its 512 are free.
	SIM_EXPECT_EQ(fixture.function.GetWritableCount(), 128);

	ClearInHalt(fixture);
	SIM_EXPECT(fixture.sim.HasArmedTransfer(0x81));
	SIM_EXPECT_EQ(HostReadPattern(fixture, 0), 128);
	SIM_EXPECT_EQ(HostReadPattern(fixture, 128), 128);
	SIM_EXPECT_EQ(HostReadPattern(fixture, 256), 128);
	SIM_EXPECT(!fixture.sim.HasArmedTransfer(0x81));
	SIM_EXPECT_EQ(fixture.function.GetWritableCount(), 512);
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


// The host deconfigures the device while the thread fills a stage. The claim ends with the session: the fill's marks go back, and the
// claim is released rather than looping. The next configuration clears the ring, so only the new session's bytes are sent.
void TestDeconfigureDuringFill() {
	CountingFixture fixture;
	fixture.Configure();
	SIM_EXPECT_EQ(WritePattern(fixture.function, 0, 384), 384);

	fixture.device.Process();
	SIM_EXPECT_EQ(fixture.function.produceCount, 1);

	fixture.function.afterProduce = [&fixture]() {
		SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x00, 9, 0, 0x0000, 0)), ResultStatus::ok);
	};

	fixture.device.Process();
	SIM_EXPECT(fixture.function.afterProduce == nullptr);
	SIM_EXPECT(!fixture.sim.HasArmedTransfer(0x81));
	SIM_EXPECT_EQ(fixture.function.GetWritableCount(), 0);

	fixture.Enumerate();
	SIM_EXPECT_EQ(fixture.function.GetWritableCount(), 512);
	SIM_EXPECT_EQ(WritePattern(fixture.function, 3000, 128), 128);
	fixture.device.Process();
	SIM_EXPECT_EQ(HostReadPattern(fixture, 3000), 128);
	SIM_EXPECT(!fixture.sim.HasArmedTransfer(0x81));
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


// A bus reset lands inside the commit that Process() makes for a completed transfer, with the claim held. The reset clears the ring,
// so the commit runs on a cleared ring, and the queue must not advance into the new session. The new session's bytes are then sent.
void TestBusResetDuringCommit() {
	CountingFixture fixture;
	fixture.Configure();
	SIM_EXPECT_EQ(WritePattern(fixture.function, 0, 384), 384);

	fixture.device.Process();
	SIM_EXPECT_EQ(fixture.function.produceCount, 1);

	// The host takes the first transfer during the thread's fill of the second stage, so its completion is pending at the end of the claim.
	std::array<uint8, 128> in = {};
	fixture.function.afterProduce = [&fixture, &in]() {
		uint32 received = 0;
		SIM_EXPECT_EQ(fixture.sim.HostIn(0x81, in, received), ResultStatus::ok);
		SIM_EXPECT_EQ(received, 128);
	};

	// The completion is committed by LeaveClaim(), and the reset lands at the start of that commit.
	fixture.function.beforeCommit = [&fixture]() {
		fixture.Enumerate();
	};

	fixture.device.Process();
	SIM_EXPECT(fixture.function.afterProduce == nullptr);
	SIM_EXPECT(fixture.function.beforeCommit == nullptr);
	SIM_EXPECT(!fixture.sim.HasArmedTransfer(0x81));
	SIM_EXPECT_EQ(fixture.function.GetWritableCount(), 512);
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);

	// Only the new session's bytes go out. The second stage of the old session is not sent.
	SIM_EXPECT_EQ(WritePattern(fixture.function, 4000, 128), 128);
	fixture.device.Process();
	SIM_EXPECT_EQ(HostReadPattern(fixture, 4000), 128);
	SIM_EXPECT(!fixture.sim.HasArmedTransfer(0x81));
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


// Writes and reads through the stages, many transfers in all, with the ring refilled as it drains. The bytes must come out
// in the order they went in, with none lost or repeated.
void TestManyTransfersInOrder() {
	CountingFixture fixture;
	fixture.Configure();

	const uint32 total = 3000;
	uint32 written = 0;
	uint32 read = 0;

	for (uint32 round = 0; round < 200 && read < total; round++) {
		if (written < total) {
			uint32 room = fixture.function.GetWritableCount();
			uint32 chunk = total - written;
			if (chunk > room) {
				chunk = room;
			}

			if (chunk > 0) {
				SIM_EXPECT_EQ(WritePattern(fixture.function, written, static_cast<uint16>(chunk)), chunk);
				written += chunk;
			}
		}

		fixture.device.Process();

		// Each read is one transfer. A transfer that completes starts the staged one, and the interrupt produces the next when
		// nothing is staged, so the loop ends when the ring is drained.
		uint32 guard = 0;
		while (fixture.sim.HasArmedTransfer(0x81) && guard < 64) {
			std::array<uint8, 128> in = {};
			uint32 received = 0;
			SIM_EXPECT_EQ(fixture.sim.HostIn(0x81, in, received), ResultStatus::ok);
			SIM_EXPECT(MatchesPattern(std::span<const uint8>(in).first(received), read));
			read += received;
			guard++;
			fixture.device.Process();
		}
	}

	SIM_EXPECT_EQ(written, total);
	SIM_EXPECT_EQ(read, total);
	SIM_EXPECT_EQ(fixture.function.GetWritableCount(), 512);
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


// The producer's state. The stream producer is a delegate with this as its context, so it needs no capture and no heap.
struct ProducerState {
	uint32 produceCalls = 0;
	uint32 produced = 0;
};


size_t ProducePattern(void* context, std::span<uint8> packets) {
	ProducerState& state = *static_cast<ProducerState*>(context);
	state.produceCalls++;
	for (std::size_t index = 0; index < packets.size(); index++) {
		packets[index] = PatternByte(state.produced);
		state.produced++;
	}

	return packets.size();
}


// The streaming function fills its stages from Process(). It keeps running on the interrupt alone once the stream is armed.
void TestStreamProducerInProcess() {
	StreamFixture fixture;
	ProducerState state;
	uint32& produceCalls = state.produceCalls;
	StreamFunction::Producer producer = StreamFunction::Producer::FromRaw(&ProducePattern, &state);
	ResultStatus setStatus = fixture.function.SetProducer(producer);
	SIM_EXPECT_EQ(setStatus, ResultStatus::ok);
	fixture.Configure();

	fixture.device.Process();
	SIM_EXPECT_EQ(produceCalls, 1);
	fixture.device.Process();
	SIM_EXPECT_EQ(produceCalls, 2);
	fixture.device.Process();
	SIM_EXPECT_EQ(produceCalls, 2);

	SIM_EXPECT_EQ(HostReadPattern(fixture, 0), 128);
	SIM_EXPECT_EQ(produceCalls, 2);
	fixture.device.Process();
	SIM_EXPECT_EQ(produceCalls, 3);
	SIM_EXPECT_EQ(HostReadPattern(fixture, 128), 128);
	SIM_EXPECT_EQ(produceCalls, 3);
	fixture.device.Process();
	SIM_EXPECT_EQ(produceCalls, 4);
	SIM_EXPECT_EQ(HostReadPattern(fixture, 256), 128);
	SIM_EXPECT_EQ(produceCalls, 4);

	// The stage behind it was produced in the interrupt, because nothing was staged: the stream is still armed.
	SIM_EXPECT_EQ(HostReadPattern(fixture, 384), 128);
	SIM_EXPECT_EQ(produceCalls, 5);
	SIM_EXPECT(fixture.sim.HasArmedTransfer(0x81));
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


}


// A sim adapter that holds one host OUT packet while nothing is armed, as the fsdev port does with a packet it received before a
// transfer was armed. StartOutTransfer() delivers the held packet from inside the call, so the completion runs inside the call.
class HoldingSimAdapter : public SimUSBDeviceAdapter {
public:
	// Packets delivered from inside StartOutTransfer().
	uint32 deliveredInCall = 0;


private:
	std::array<uint8, 64> held{};
	uint32 heldLength = 0;


public:
	// Keeps a packet the host sends while no transfer is armed, instead of answering NAK.
	void HoldPacket(std::span<const uint8> packet) {
		heldLength = static_cast<uint32>(packet.size());
		for (uint32 index = 0; index < heldLength; index++) {
			held[index] = packet[index];
		}
	}


	ResultStatus StartOutTransfer(uint8 address, std::span<uint8> buffer) override {
		ResultStatus status = SimUSBDeviceAdapter::StartOutTransfer(address, buffer);
		if (status != ResultStatus::ok || heldLength == 0) {
			return status;
		}

		uint32 length = heldLength;
		heldLength = 0;
		deliveredInCall++;
		static_cast<void>(HostOut(address, std::span<const uint8>(held).first(length)));
		return status;
	}
};


struct HoldingFixture {
	HoldingSimAdapter sim;
	VendorFunction function;
	std::array<USBFunction*, 1> functions;
	USBDevice device;


	HoldingFixture()
		: function(Interfaces, Endpoints),
		  functions{ &function },
		  device(sim, SimDescriptors::SimpleSet, functions) {
	}


	void Configure() {
		SIM_EXPECT_EQ(device.Start(USBDeviceAdapter::Parameters()), ResultStatus::ok);
		sim.InjectBusReset();
		SIM_EXPECT_EQ(SimHost::Control(sim, SimHost::MakeSetup(0x00, 5, 5, 0x0000, 0)), ResultStatus::ok);
		SIM_EXPECT_EQ(SimHost::Control(sim, SimHost::MakeSetup(0x00, 9, 1, 0x0000, 0)), ResultStatus::ok);
		SIM_EXPECT_EQ(device.GetState(), USBDeviceState::Configured);
	}
};


// The receive ring holds 256 bytes and an OUT transfer needs one packet (64 bytes) free, so after four full packets the endpoint
// cannot be armed. A 10-byte packet the host sends then has nowhere to go and is held. The application reads 64 bytes, and Process()
// arms the endpoint: the held packet is delivered inside that call, and its bytes are stored after the critical section, with the
// endpoint armed again only when a whole packet fits. The checks read the bytes back in order, so a lost, moved or duplicated byte shows.
void TestOutHeldPacketDeliveredFromProcess() {
	HoldingFixture fixture;
	fixture.Configure();

	std::array<uint8, 256> pattern = {};
	FillPattern(pattern);
	for (std::size_t packet = 0; packet < 4; packet++) {
		SIM_EXPECT_EQ(fixture.sim.HostOut(0x01, std::span<const uint8>(pattern).subspan(packet * 64, 64)).Value(), 64U);
	}

	SIM_EXPECT_EQ(fixture.function.GetReadableCount(), 256);
	SIM_EXPECT(!fixture.sim.HasArmedTransfer(0x01));

	std::array<uint8, 10> packet = {};
	for (std::size_t index = 0; index < packet.size(); index++) {
		packet[index] = static_cast<uint8>(0xA0 + index);
	}

	SIM_EXPECT_EQ(fixture.sim.HostOut(0x01, packet).Error(), ResultStatus::notReady);
	fixture.sim.HoldPacket(packet);

	std::array<uint8, 64> first = {};
	SIM_EXPECT_EQ(fixture.function.Read(first), 64);
	SIM_EXPECT(SameBytes(std::span<const uint8>(first), std::span<const uint8>(pattern).first(64)));

	// The room of one packet is armed, and the held packet arrives in it. 256 - 64 read + 10 held is 202 bytes, and the 10 bytes
	// leave less than a packet free, so the endpoint stays unarmed.
	fixture.device.Process();
	SIM_EXPECT_EQ(fixture.sim.deliveredInCall, 1U);
	SIM_EXPECT_EQ(fixture.function.GetReadableCount(), 192 + 10);
	SIM_EXPECT(!fixture.sim.HasArmedTransfer(0x01));

	std::array<uint8, 256> rest = {};
	SIM_EXPECT_EQ(fixture.function.Read(rest), 202);
	SIM_EXPECT(SameBytes(std::span<const uint8>(rest).first(192), std::span<const uint8>(pattern).subspan(64, 192)));
	SIM_EXPECT(SameBytes(std::span<const uint8>(rest).subspan(192, 10), std::span<const uint8>(packet)));
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


// A host write is sent as its packets, one OUT transfer at a time, and no zero-length packet follows it. The host sends the next
// packet as soon as the endpoint is armed again, as a host does. A 64-byte write and a 128-byte write both reach Read() whole, with
// the endpoint armed after each. Before the fix the OUT transfer was four packets long, and these bytes waited in it.
template<typename TFixture>
void HostWrite(TFixture& fixture, std::span<const uint8> data) {
	std::size_t sent = 0;
	while (sent < data.size()) {
		Result<uint32> stored = fixture.sim.HostOut(0x01, data.subspan(sent));
		SIM_EXPECT(stored.IsOk());
		if (!stored.IsOk() || stored.Value() == 0) {
			break;
		}

		sent += stored.Value();
	}

	SIM_EXPECT_EQ(sent, data.size());
}


void TestWholePacketWritesReachRead() {
	VendorFixture fixture;
	fixture.Configure();

	std::array<uint8, 128> data = {};
	FillPattern(data);
	std::array<uint8, 128> read = {};

	HostWrite(fixture, std::span<const uint8>(data).first(64));
	SIM_EXPECT_EQ(fixture.function.GetReadableCount(), 64);
	SIM_EXPECT(fixture.sim.HasArmedTransfer(0x01));
	SIM_EXPECT_EQ(fixture.function.Read(read), 64);
	SIM_EXPECT(SameBytes(std::span<const uint8>(read).first(64), std::span<const uint8>(data).first(64)));

	HostWrite(fixture, data);
	SIM_EXPECT_EQ(fixture.function.GetReadableCount(), 128);
	SIM_EXPECT(fixture.sim.HasArmedTransfer(0x01));
	SIM_EXPECT_EQ(fixture.function.Read(read), 128);
	SIM_EXPECT(SameBytes(std::span<const uint8>(read), std::span<const uint8>(data)));
	SIM_EXPECT_EQ(fixture.function.GetReadableCount(), 0);
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


// The vendor raw channel of the first hardware run: VendorBulkFunction<512, 512, 4>. Its OUT transfer used to be four packets, so a
// 64-byte write hung there (the looped counter stayed at 64). Both writes reach Read() whole now.
using WideOutFixture = FixtureOf<VendorBulkFunction<512, 512, 4>>;


void TestWholePacketWritesWithFourPacketInTransfers() {
	WideOutFixture fixture;
	fixture.Configure();

	std::array<uint8, 128> data = {};
	FillPattern(data);
	std::array<uint8, 128> read = {};

	HostWrite(fixture, std::span<const uint8>(data).first(64));
	SIM_EXPECT_EQ(fixture.function.GetReadableCount(), 64);
	SIM_EXPECT_EQ(fixture.function.Read(read), 64);
	SIM_EXPECT(SameBytes(std::span<const uint8>(read).first(64), std::span<const uint8>(data).first(64)));

	HostWrite(fixture, data);
	SIM_EXPECT_EQ(fixture.function.Read(read), 128);
	SIM_EXPECT(SameBytes(std::span<const uint8>(read), std::span<const uint8>(data)));
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


// A halt of the bulk OUT endpoint NAKs the host's data; after the clear, the host's packets are taken. The same case as the CDC-ACM
// data interface's, on the vendor pair's OUT 0x01.
void TestOutHaltNaksThenAccepts() {
	VendorFixture fixture;
	fixture.Configure();
	SIM_EXPECT(fixture.sim.HasArmedTransfer(0x01));

	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x02, 3, 0x0000, 0x0001, 0)), ResultStatus::ok);
	SIM_EXPECT(!fixture.sim.HasArmedTransfer(0x01));
	// A halted endpoint answers the host's packet with STALL, which the simulator reports as accessError.
	std::array<uint8, 64> data = {};
	FillPattern(data);
	SIM_EXPECT_EQ(fixture.sim.HostOut(0x01, data).Error(), ResultStatus::accessError);

	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x02, 1, 0x0000, 0x0001, 0)), ResultStatus::ok);
	SIM_EXPECT(fixture.sim.HasArmedTransfer(0x01));
	SIM_EXPECT(fixture.sim.HostOut(0x01, data).IsOk());
	SIM_EXPECT(fixture.sim.HostOut(0x01, std::span<const uint8>()).IsOk());
	SIM_EXPECT_EQ(fixture.function.GetReadableCount(), 64);
	std::array<uint8, 64> read = {};
	SIM_EXPECT_EQ(fixture.function.Read(read), 64);
	SIM_EXPECT(SameBytes(std::span<const uint8>(read), std::span<const uint8>(data)));
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


// The zero-length packet on the buffered vendor pair, through the DATA PID as in TestCdcAcmData: the stage is 128 bytes (two packets)
// in CountingFunction, and a full stage with nothing behind it ends with one. The same three cases as the CDC-ACM data interface.
void TestWriteOfFullStageEndsWithZeroLengthPacket() {
	CountingFixture fixture;
	fixture.Configure();
	SIM_EXPECT_EQ(WritePattern(fixture.function, 0, 128), 128);

	fixture.device.Process();
	bool toggleBefore = fixture.sim.GetDataToggle(0x81);
	SIM_EXPECT_EQ(HostReadPattern(fixture, 0), 128);
	SIM_EXPECT(fixture.sim.GetDataToggle(0x81) != toggleBefore);
	SIM_EXPECT(!fixture.sim.HasArmedTransfer(0x81));
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


void TestWriteOfTwoStagesZeroLengthPacketOnlyAfterLast() {
	CountingFixture fixture;
	fixture.Configure();
	SIM_EXPECT_EQ(WritePattern(fixture.function, 0, 256), 256);

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


void TestWriteOf64KeepsZeroLengthPacket() {
	CountingFixture fixture;
	fixture.Configure();
	SIM_EXPECT_EQ(WritePattern(fixture.function, 0, 64), 64);

	fixture.device.Process();
	bool toggleBefore = fixture.sim.GetDataToggle(0x81);
	SIM_EXPECT_EQ(HostReadPattern(fixture, 0), 64);
	SIM_EXPECT(fixture.sim.GetDataToggle(0x81) == toggleBefore);
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


// The stream's consumer records each packet it is given: how many calls, the largest call, and whether the bytes match the pattern.
struct ConsumerState {
	uint32 calls = 0;
	uint32 received = 0;
	std::size_t largest = 0;
	bool matches = true;
};


void ConsumePattern(void* context, std::span<const uint8> packets) {
	ConsumerState& state = *static_cast<ConsumerState*>(context);
	state.calls++;
	if (packets.size() > state.largest) {
		state.largest = packets.size();
	}

	if (!MatchesPattern(packets, state.received)) {
		state.matches = false;
	}

	state.received += static_cast<uint32>(packets.size());
}


// A host write of N whole packets, each packet one OUT transfer and no zero-length packet after the write: the consumer gets every
// byte, one call per packet. The writes are 64, 128 and 576 bytes. 576 is not a multiple of the old 512-byte OUT transfer, which is
// the case that stayed armed before the fix.
void TestStreamOutDeliversEveryPacket() {
	ConsumerState state;
	StreamFixture fixture;
	SIM_EXPECT_EQ(fixture.function.SetConsumer(StreamFunction::Consumer::FromRaw(&ConsumePattern, &state)), ResultStatus::ok);
	fixture.Configure();
	SIM_EXPECT(fixture.sim.HasArmedTransfer(0x01));

	// One stream of 768 bytes, sent as three writes in order, so the consumer's position check runs across the writes.
	std::array<uint8, 768> data = {};
	for (std::size_t index = 0; index < data.size(); index++) {
		data[index] = PatternByte(static_cast<uint32>(index));
	}

	std::span<const uint8> stream(data);
	HostWrite(fixture, stream.first(64));
	SIM_EXPECT_EQ(state.received, 64);
	SIM_EXPECT_EQ(state.calls, 1);
	SIM_EXPECT(fixture.sim.HasArmedTransfer(0x01));

	HostWrite(fixture, stream.subspan(64, 128));
	SIM_EXPECT_EQ(state.received, 192);
	SIM_EXPECT_EQ(state.calls, 3);
	SIM_EXPECT(fixture.sim.HasArmedTransfer(0x01));

	HostWrite(fixture, stream.subspan(192, 576));
	SIM_EXPECT_EQ(state.received, 768);
	SIM_EXPECT_EQ(state.calls, 12);
	SIM_EXPECT_EQ(state.largest, 64U);
	SIM_EXPECT(state.matches);
	SIM_EXPECT(fixture.sim.HasArmedTransfer(0x01));
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


// The stream's producer for the zero-length packet cases: it hands out limit bytes of the pattern, then nothing.
struct LimitedState {
	uint32 limit = 0;
	uint32 produced = 0;
};


size_t ProduceLimited(void* context, std::span<uint8> packets) {
	LimitedState& state = *static_cast<LimitedState*>(context);
	size_t count = packets.size();
	if (count > state.limit - state.produced) {
		count = state.limit - state.produced;
	}

	for (std::size_t index = 0; index < count; index++) {
		packets[index] = PatternByte(state.produced + static_cast<uint32>(index));
	}

	state.produced += static_cast<uint32>(count);
	return count;
}


// A stream of one stage, 128 bytes, and nothing after it. Two packets flip the DATA PID twice, so it is as it was before the transfer.
// A zero-length packet would flip it a third time. The host's read is the length it asked for, and it never ends early.
void TestStreamStageEndsWithoutZeroLengthPacket() {
	StreamFixture fixture;
	LimitedState state;
	state.limit = 128;
	SIM_EXPECT_EQ(fixture.function.SetProducer(StreamFunction::Producer::FromRaw(&ProduceLimited, &state)), ResultStatus::ok);
	fixture.Configure();

	fixture.device.Process();
	SIM_EXPECT(fixture.sim.HasArmedTransfer(0x81));
	bool toggleBefore = fixture.sim.GetDataToggle(0x81);
	SIM_EXPECT_EQ(HostReadPattern(fixture, 0), 128);
	SIM_EXPECT(fixture.sim.GetDataToggle(0x81) == toggleBefore);

	// The producer has nothing more, so neither the interrupt nor Process() arms another stage.
	SIM_EXPECT(!fixture.sim.HasArmedTransfer(0x81));
	fixture.device.Process();
	SIM_EXPECT(!fixture.sim.HasArmedTransfer(0x81));
	SIM_EXPECT_EQ(state.produced, 128);
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


// A stream whose last stage is one whole packet, 64 bytes. The transfer has one packet, so the DATA PID flips once, and no
// zero-length packet follows it.
void TestStreamPartialStageEndsWithoutZeroLengthPacket() {
	StreamFixture fixture;
	LimitedState state;
	state.limit = 64;
	SIM_EXPECT_EQ(fixture.function.SetProducer(StreamFunction::Producer::FromRaw(&ProduceLimited, &state)), ResultStatus::ok);
	fixture.Configure();

	fixture.device.Process();
	SIM_EXPECT(fixture.sim.HasArmedTransfer(0x81));
	bool toggleBefore = fixture.sim.GetDataToggle(0x81);
	SIM_EXPECT_EQ(HostReadPattern(fixture, 0), 64);
	SIM_EXPECT(fixture.sim.GetDataToggle(0x81) != toggleBefore);
	SIM_EXPECT(!fixture.sim.HasArmedTransfer(0x81));
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


// Three full stages produced back to back and then nothing: each 128-byte transfer leaves the DATA PID as it was, so none of the three
// has a zero-length packet, and the bytes come out in order.
void TestStreamStagesWithoutZeroLengthPackets() {
	StreamFixture fixture;
	LimitedState state;
	state.limit = 384;
	SIM_EXPECT_EQ(fixture.function.SetProducer(StreamFunction::Producer::FromRaw(&ProduceLimited, &state)), ResultStatus::ok);
	fixture.Configure();

	fixture.device.Process();
	fixture.device.Process();
	SIM_EXPECT_EQ(state.produced, 256);

	bool toggleBefore = fixture.sim.GetDataToggle(0x81);
	SIM_EXPECT_EQ(HostReadPattern(fixture, 0), 128);
	SIM_EXPECT(fixture.sim.GetDataToggle(0x81) == toggleBefore);

	toggleBefore = fixture.sim.GetDataToggle(0x81);
	SIM_EXPECT_EQ(HostReadPattern(fixture, 128), 128);
	SIM_EXPECT(fixture.sim.GetDataToggle(0x81) == toggleBefore);

	toggleBefore = fixture.sim.GetDataToggle(0x81);
	SIM_EXPECT_EQ(HostReadPattern(fixture, 256), 128);
	SIM_EXPECT(fixture.sim.GetDataToggle(0x81) == toggleBefore);
	SIM_EXPECT(!fixture.sim.HasArmedTransfer(0x81));
	SIM_EXPECT_EQ(state.produced, 384);
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


// Takes the armed IN transfer, of up to 512 bytes, and checks it against the pattern from position. Returns the count received.
template<typename TFixture>
uint32 HostReadWide(TFixture& fixture, uint32 position) {
	std::array<uint8, 512> in = {};
	uint32 received = 0;
	SIM_EXPECT_EQ(fixture.sim.HostIn(0x81, in, received), ResultStatus::ok);
	SIM_EXPECT(MatchesPattern(std::span<const uint8>(in).first(received), position));
	return received;
}


// The buffered function with 512-byte IN stages (eight packets), so a 448-byte stage is a whole-packet stage below the stage size.
using WideStageFixture = FixtureOf<VendorBulkFunction<1024, 256, 8>>;


// A 64-byte stage starts with nothing behind it, so its packet count is fixed at its start and it keeps its zero-length packet. The
// 448-byte stage starts after that one completes, with 64 more bytes queued behind it, so it gets none: the host's read continues into the
// last stage. That last stage, 64 bytes with nothing behind it, ends with a zero-length packet. The toggles show each packet count.
void TestWholePacketStageWithBytesBehindHasNoZeroLengthPacket() {
	WideStageFixture fixture;
	fixture.Configure();

	SIM_EXPECT_EQ(WritePattern(fixture.function, 0, 64), 64);
	fixture.device.Process();
	SIM_EXPECT(fixture.sim.HasArmedTransfer(0x81));
	bool toggleFirst = fixture.sim.GetDataToggle(0x81);

	// The 448 bytes are written while the first stage is in flight, and the thread fills them into the second stage.
	SIM_EXPECT_EQ(WritePattern(fixture.function, 64, 448), 448);
	fixture.device.Process();
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
	SIM_EXPECT_EQ(WritePattern(fixture.function, 512, 64), 64);

	// The first stage completes with its zero-length packet. The second starts from the completion, and nothing is armed with a
	// zero-length packet until the second stage completes.
	SIM_EXPECT_EQ(HostReadWide(fixture, 0), 64);
	SIM_EXPECT(fixture.sim.GetDataToggle(0x81) == toggleFirst);
	SIM_EXPECT(fixture.sim.HasArmedTransfer(0x81));
	bool toggleSecond = fixture.sim.GetDataToggle(0x81);
	SIM_EXPECT_EQ(HostReadWide(fixture, 64), 448);
	SIM_EXPECT(fixture.sim.GetDataToggle(0x81) != toggleSecond);

	// The third stage starts from the completion, because nothing was staged and the interrupt produces it. Nothing is behind it.
	SIM_EXPECT(fixture.sim.HasArmedTransfer(0x81));
	bool toggleThird = fixture.sim.GetDataToggle(0x81);
	SIM_EXPECT_EQ(HostReadWide(fixture, 512), 64);
	SIM_EXPECT(fixture.sim.GetDataToggle(0x81) == toggleThird);
	SIM_EXPECT(!fixture.sim.HasArmedTransfer(0x81));
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


// The sequence as written in the review: a 64-byte write is started, then a 448-byte write fills the second stage while the first is in
// flight. The first stage was started with nothing behind it, so its zero-length packet is decided then and stays. The second has nothing
// behind it when it starts, so it ends with one too. Both stages end with a zero-length packet.
void TestWriteBehindInFlightStageKeepsZeroLengthPackets() {
	WideStageFixture fixture;
	fixture.Configure();

	SIM_EXPECT_EQ(WritePattern(fixture.function, 0, 64), 64);
	fixture.device.Process();
	bool toggleFirst = fixture.sim.GetDataToggle(0x81);
	SIM_EXPECT_EQ(WritePattern(fixture.function, 64, 448), 448);
	fixture.device.Process();

	SIM_EXPECT_EQ(HostReadWide(fixture, 0), 64);
	SIM_EXPECT(fixture.sim.GetDataToggle(0x81) == toggleFirst);
	SIM_EXPECT(fixture.sim.HasArmedTransfer(0x81));
	bool toggleSecond = fixture.sim.GetDataToggle(0x81);
	SIM_EXPECT_EQ(HostReadWide(fixture, 64), 448);
	SIM_EXPECT(fixture.sim.GetDataToggle(0x81) == toggleSecond);
	SIM_EXPECT(!fixture.sim.HasArmedTransfer(0x81));
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


int main() {
	TestStreamStageEndsWithoutZeroLengthPacket();
	TestStreamPartialStageEndsWithoutZeroLengthPacket();
	TestStreamStagesWithoutZeroLengthPackets();
	TestWholePacketStageWithBytesBehindHasNoZeroLengthPacket();
	TestWriteBehindInFlightStageKeepsZeroLengthPackets();
	TestOutHaltNaksThenAccepts();
	TestWriteOfFullStageEndsWithZeroLengthPacket();
	TestWriteOfTwoStagesZeroLengthPacketOnlyAfterLast();
	TestWriteOf64KeepsZeroLengthPacket();
	TestStreamOutDeliversEveryPacket();
	TestWholePacketWritesWithFourPacketInTransfers();
	TestWholePacketWritesReachRead();
	TestOutHeldPacketDeliveredFromProcess();
	TestInFifoEmptyNaks();
	TestInFifoFullAndWrap();
	TestInTransferEndsOnPacketBoundary();
	TestOutFifoFullAndNak();
	TestOutZeroLengthPacketAndBoundary();
	TestEndpointPair(Endpoints, 0x01, false);
	TestSeparateNumberPairBuffering();
	TestStagesBackToBack();
	TestStagedShortTransferEndsWithZeroLengthPacket();
	TestInterruptFallbackOnly();
	TestHaltWithOneInFlightAndOneStaged();
	TestBusResetWithOneInFlightAndOneStaged();
	TestReconfigurationWithStagesInFlight();
	TestCompletionDuringFill();
	TestHaltDuringFill();
	TestDeconfigureDuringFill();
	TestBusResetDuringCommit();
	TestManyTransfersInOrder();
	TestStreamProducerInProcess();
	return SimTest::Finish("TestVendorBulk");
}
