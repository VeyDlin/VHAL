#include <VHAL.h>
#include <DemoUSBBench/Descriptors/BenchDescriptors.h>
#include <DemoUSBBench/Function/BenchFunction.h>
#include <Drivers/Interface/USB/Device/USBDevice.h>
#include <BenchProtocol.h>
#include "SimAssert.h"
#include "SimHost.h"
#include "SimUSBDeviceAdapter.h"
#include <array>
#include <cstddef>
#include <functional>
#include <span>


// The benchmark function on the simulated controller, with the bench's own descriptors (BenchUsb), as usbbench drives it: vendor
// requests on the interface, bulk IN 0x81, bulk OUT 0x02, 64-byte packets, 2048-byte IN stages (32 packets). The expected bytes
// come from the pattern definition in BenchProtocol.h (xorshift32 words from PatternSeed, least significant byte first), written
// out again below, so the test does not check the bench's generator against itself. The expected counts are the transfer sizes.
namespace {


// The serial number string of the device, in static storage: the device keeps the span.
constexpr uint8 SerialNumber[] = {
	0x0E, 0x03, 0x31, 0x00, 0x32, 0x00, 0x41, 0x00, 0x42, 0x00, 0x43, 0x00, 0x44, 0x00
};


// The first 6144 bytes of the pattern from the seed, built from the definition alone.
std::array<uint8, 6144> BuildPattern() {
	std::array<uint8, 6144> bytes{};
	uint32 state = 0x12345678;
	for (std::size_t word = 0; word < bytes.size() / 4; word++) {
		state ^= state << 13;
		state ^= state >> 17;
		state ^= state << 5;
		bytes[word * 4 + 0] = static_cast<uint8>(state);
		bytes[word * 4 + 1] = static_cast<uint8>(state >> 8);
		bytes[word * 4 + 2] = static_cast<uint8>(state >> 16);
		bytes[word * 4 + 3] = static_cast<uint8>(state >> 24);
	}

	return bytes;
}


const std::array<uint8, 6144>& Pattern() {
	static const std::array<uint8, 6144> bytes = BuildPattern();
	return bytes;
}


// Reads a little-endian field of the counter block.
uint64 ReadLittle(std::span<const uint8> bytes, std::size_t offset, std::size_t size) {
	uint64 value = 0;
	for (std::size_t index = 0; index < size; index++) {
		value |= static_cast<uint64>(bytes[offset + index]) << (8 * index);
	}

	return value;
}


template<typename TFunction>
struct BenchFixtureOf {
	SimUSBDeviceAdapter sim;
	TFunction function;
	std::array<USBFunction*, 1> functions;
	USBDevice device;


	BenchFixtureOf()
		: function(BenchUsb::Function(0)),
		  functions{ &function },
		  device(sim, BenchUsb::Set, functions) {
	}


	// Gives the device its serial number, starts it and configures it, as a host does.
	void Configure() {
		SIM_EXPECT_EQ(device.SetSerialNumber(SerialNumber), ResultStatus::ok);
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


	// SetMode, the vendor request OUT on the interface with no data.
	void SetMode(BenchProtocol::Mode mode) {
		uint8 request = static_cast<uint8>(BenchProtocol::Request::SetMode);
		SIM_EXPECT_EQ(SimHost::Control(sim, SimHost::MakeSetup(0x41, request, static_cast<uint16>(mode), BenchProtocol::InterfaceNumber, 0)), ResultStatus::ok);
	}


	// GetCounters, the vendor request IN on the interface: 32 little-endian bytes.
	BenchProtocol::Counters ReadCounters() {
		uint8 request = static_cast<uint8>(BenchProtocol::Request::GetCounters);
		std::array<uint8, BenchProtocol::CountersSize> in = {};
		uint32 received = 0;
		SIM_EXPECT_EQ(SimHost::Control(sim, SimHost::MakeSetup(0xC1, request, 0, BenchProtocol::InterfaceNumber, BenchProtocol::CountersSize), std::span<const uint8>(), in, received), ResultStatus::ok);
		SIM_EXPECT_EQ(received, BenchProtocol::CountersSize);

		BenchProtocol::Counters counters{};
		counters.bytesSent = ReadLittle(in, 0, 8);
		counters.bytesReceived = ReadLittle(in, 8, 8);
		counters.receiveErrors = static_cast<uint32>(ReadLittle(in, 16, 4));
		counters.firstErrorOffset = static_cast<uint32>(ReadLittle(in, 20, 4));
		counters.busResets = static_cast<uint32>(ReadLittle(in, 24, 4));
		counters.usbErrors = static_cast<uint32>(ReadLittle(in, 28, 4));
		return counters;
	}


	// The host's bulk OUT, one packet per transfer: each packet must be stored whole, which the bench counts as received.
	void SendPackets(std::span<const uint8> data) {
		for (std::size_t offset = 0; offset < data.size(); offset += BenchProtocol::BulkPacketSize) {
			Result<uint32> stored = sim.HostOut(BenchProtocol::BulkOutAddress, data.subspan(offset, BenchProtocol::BulkPacketSize));
			SIM_EXPECT(stored.IsOk());
			SIM_EXPECT_EQ(stored.Value(), static_cast<uint32>(BenchProtocol::BulkPacketSize));
		}
	}
};


using BenchFixture = BenchFixtureOf<DemoUSBBench::BenchFunction>;


// The function with hooks for the window that LeaveClaim() leaves open. afterProduce runs after the thread's fill, inside the claim.
// beforeCommit runs at the start of the commit that LeaveClaim() makes outside any section, after the claim's completion is seen.
class HookedBenchFunction : public DemoUSBBench::BenchFunction {
public:
	std::function<void()> afterProduce;
	std::function<void()> beforeCommit;


	explicit HookedBenchFunction(const USBFunctionLayout& layout)
		: BenchFunction(layout) {
	}


protected:
	size_t ProduceIn(std::span<uint8> packets) override {
		size_t count = BenchFunction::ProduceIn(packets);
		if (afterProduce != nullptr) {
			std::function<void()> hook = afterProduce;
			afterProduce = nullptr;
			hook();
		}

		return count;
	}


	void CommitIn(size_t count) override {
		if (beforeCommit != nullptr) {
			std::function<void()> hook = beforeCommit;
			beforeCommit = nullptr;
			hook();
		}

		BenchFunction::CommitIn(count);
	}
};


// Idle: nothing is armed on bulk IN, so the host gets NAK. Bulk OUT is accepted and dropped, and only its bytes are counted.
void TestIdleSendsNothing() {
	BenchFixture fixture;
	fixture.Configure();
	fixture.device.Process();
	SIM_EXPECT(!fixture.sim.HasArmedTransfer(0x81));

	std::array<uint8, 2048> in = {};
	uint32 received = 0;
	SIM_EXPECT_EQ(fixture.sim.HostIn(0x81, in, received), ResultStatus::notReady);

	fixture.SendPackets(std::span<const uint8>(Pattern()).first(64));
	BenchProtocol::Counters counters = fixture.ReadCounters();
	SIM_EXPECT_EQ(counters.bytesSent, 0);
	SIM_EXPECT_EQ(counters.bytesReceived, 64);
	SIM_EXPECT_EQ(counters.receiveErrors, 0);
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


// Source: each read takes one 2048-byte stage, and the stages follow each other in the pattern. A whole number of packets ends with no
// zero-length packet, so the DATA PID is as it was after the 32 packets. The counters count a stage once the host has read it.
void TestSourceStreamsPattern() {
	BenchFixture fixture;
	fixture.Configure();
	fixture.SetMode(BenchProtocol::Mode::Source);
	fixture.device.Process();

	std::array<uint8, 2048> in = {};
	for (std::size_t stage = 0; stage < 3; stage++) {
		SIM_EXPECT(fixture.sim.HasArmedTransfer(0x81));
		bool toggleBefore = fixture.sim.GetDataToggle(0x81);
		uint32 received = 0;
		SIM_EXPECT_EQ(fixture.sim.HostIn(0x81, in, received), ResultStatus::ok);
		SIM_EXPECT_EQ(received, 2048);
		SIM_EXPECT(SimHost::SameBytes(in, std::span<const uint8>(Pattern()).subspan(stage * 2048, 2048)));
		SIM_EXPECT(fixture.sim.GetDataToggle(0x81) == toggleBefore);
		fixture.device.Process();
	}

	BenchProtocol::Counters counters = fixture.ReadCounters();
	SIM_EXPECT_EQ(counters.bytesSent, 3 * 2048);
	SIM_EXPECT_EQ(counters.bytesReceived, 0);
	SIM_EXPECT_EQ(counters.receiveErrors, 0);
	SIM_EXPECT_EQ(counters.firstErrorOffset, 0xFFFFFFFFU);
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


// Sink: the host's packets are checked against the pattern from the seed. 64 packets (4096 bytes) of pattern are clean. The next packet
// has one byte changed, at offset 5 of the packet, so one error is counted and its stream offset is 4096 + 5.
void TestSinkChecksPattern() {
	BenchFixture fixture;
	fixture.Configure();
	fixture.SetMode(BenchProtocol::Mode::Sink);

	fixture.SendPackets(std::span<const uint8>(Pattern()).first(4096));
	BenchProtocol::Counters counters = fixture.ReadCounters();
	SIM_EXPECT_EQ(counters.bytesReceived, 4096);
	SIM_EXPECT_EQ(counters.receiveErrors, 0);
	SIM_EXPECT_EQ(counters.firstErrorOffset, 0xFFFFFFFFU);

	std::array<uint8, 64> packet = {};
	for (std::size_t index = 0; index < packet.size(); index++) {
		packet[index] = Pattern()[4096 + index];
	}

	packet[5] = static_cast<uint8>(packet[5] ^ 0xFF);
	fixture.SendPackets(packet);
	counters = fixture.ReadCounters();
	SIM_EXPECT_EQ(counters.bytesReceived, 4160);
	SIM_EXPECT_EQ(counters.receiveErrors, 1);
	SIM_EXPECT_EQ(counters.firstErrorOffset, 4101);
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


// SourceAndSink: each direction has its own generator, both from the seed. One stage of pattern goes out on bulk IN and one packet of
// pattern comes in on bulk OUT, and the bench checks the packet and counts the stage.
void TestSourceAndSinkBothWays() {
	BenchFixture fixture;
	fixture.Configure();
	fixture.SetMode(BenchProtocol::Mode::SourceAndSink);
	fixture.device.Process();

	fixture.SendPackets(std::span<const uint8>(Pattern()).first(64));

	std::array<uint8, 2048> in = {};
	uint32 received = 0;
	SIM_EXPECT_EQ(fixture.sim.HostIn(0x81, in, received), ResultStatus::ok);
	SIM_EXPECT_EQ(received, 2048);
	SIM_EXPECT(SimHost::SameBytes(in, std::span<const uint8>(Pattern()).first(2048)));

	BenchProtocol::Counters counters = fixture.ReadCounters();
	SIM_EXPECT_EQ(counters.bytesSent, 2048);
	SIM_EXPECT_EQ(counters.bytesReceived, 64);
	SIM_EXPECT_EQ(counters.receiveErrors, 0);
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


// Loopback: the bytes the host writes come back on bulk IN unchanged. 256 bytes written as four packets are read back as one transfer of
// 256 bytes: four packets and no zero-length packet, so the DATA PID is as it was. A second write of 128 bytes comes back the same way.
void TestLoopbackReturnsBytes() {
	BenchFixture fixture;
	fixture.Configure();
	fixture.SetMode(BenchProtocol::Mode::Loopback);
	fixture.SendPackets(std::span<const uint8>(Pattern()).first(256));
	fixture.device.Process();
	SIM_EXPECT(fixture.sim.HasArmedTransfer(0x81));

	bool toggleBefore = fixture.sim.GetDataToggle(0x81);
	std::array<uint8, 2048> in = {};
	uint32 received = 0;
	SIM_EXPECT_EQ(fixture.sim.HostIn(0x81, in, received), ResultStatus::ok);
	SIM_EXPECT_EQ(received, 256);
	SIM_EXPECT(SimHost::SameBytes(std::span<const uint8>(in).first(256), std::span<const uint8>(Pattern()).first(256)));
	SIM_EXPECT(fixture.sim.GetDataToggle(0x81) == toggleBefore);

	BenchProtocol::Counters counters = fixture.ReadCounters();
	SIM_EXPECT_EQ(counters.bytesSent, 256);
	SIM_EXPECT_EQ(counters.bytesReceived, 256);

	fixture.SendPackets(std::span<const uint8>(Pattern()).subspan(256, 128));
	fixture.device.Process();
	SIM_EXPECT(fixture.sim.HasArmedTransfer(0x81));
	SIM_EXPECT_EQ(fixture.sim.HostIn(0x81, in, received), ResultStatus::ok);
	SIM_EXPECT_EQ(received, 128);
	SIM_EXPECT(SimHost::SameBytes(std::span<const uint8>(in).first(128), std::span<const uint8>(Pattern()).subspan(256, 128)));

	counters = fixture.ReadCounters();
	SIM_EXPECT_EQ(counters.bytesSent, 384);
	SIM_EXPECT_EQ(counters.bytesReceived, 384);
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


// A bus reset that lands in the commit LeaveClaim() makes for a completed stage. The stage was sent, so the commit counts it. The next
// stage was staged and never sent, so it goes back once, to the first byte it holds (2048), and the new session sends it from there.
// Before the fix the reset emptied the staged list, and the commit's pop asserted, which aborted the device.
void TestBusResetInsideCommitKeepsUnsentStage() {
	BenchFixtureOf<HookedBenchFunction> fixture;
	fixture.Configure();
	fixture.SetMode(BenchProtocol::Mode::Source);
	fixture.device.Process();
	SIM_EXPECT(fixture.sim.HasArmedTransfer(0x81));

	// The host reads the first stage during the thread's fill of the second, so its completion is pending when the claim ends.
	std::array<uint8, 2048> in = {};
	uint32 received = 0;
	bool hostRead = false;
	fixture.function.afterProduce = [&fixture, &in, &received, &hostRead]() {
		SIM_EXPECT_EQ(fixture.sim.HostIn(0x81, in, received), ResultStatus::ok);
		hostRead = true;
	};

	// The bus is reset at the start of that commit, with the claim still held.
	bool reset = false;
	fixture.function.beforeCommit = [&fixture, &reset]() {
		fixture.Enumerate();
		reset = true;
	};

	fixture.device.Process();
	SIM_EXPECT(hostRead);
	SIM_EXPECT(reset);
	SIM_EXPECT_EQ(received, 2048);
	SIM_EXPECT(SimHost::SameBytes(in, std::span<const uint8>(Pattern()).first(2048)));
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);

	// The new session's first stage starts from the unsent bytes, which begin at 2048.
	fixture.device.Process();
	SIM_EXPECT(fixture.sim.HasArmedTransfer(0x81));
	SIM_EXPECT_EQ(fixture.sim.HostIn(0x81, in, received), ResultStatus::ok);
	SIM_EXPECT_EQ(received, 2048);
	SIM_EXPECT(SimHost::SameBytes(in, std::span<const uint8>(Pattern()).subspan(2048, 2048)));

	BenchProtocol::Counters counters = fixture.ReadCounters();
	SIM_EXPECT_EQ(counters.bytesSent, 4096);
	SIM_EXPECT_EQ(counters.busResets, 1);
	SIM_EXPECT_EQ(counters.receiveErrors, 0);
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


}


int main() {
	TestIdleSendsNothing();
	TestSourceStreamsPattern();
	TestSinkChecksPattern();
	TestSourceAndSinkBothWays();
	TestLoopbackReturnsBytes();
	TestBusResetInsideCommitKeepsUnsentStage();
	return SimTest::Finish("TestBenchModes");
}
