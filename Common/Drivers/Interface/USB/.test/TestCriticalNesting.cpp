#include <VHAL.h>
#include "Descriptors.h"
#include "SimAssert.h"
#include "SimHost.h"
#include "SimUSBDeviceAdapter.h"
#include <Drivers/Interface/USB/Class/Vendor/VendorBulkFunction.h>
#include <Drivers/Interface/USB/Device/USBDevice.h>
#include <array>
#include <span>

// Critical sections nest on every port. The checks here hold the vendor bulk function to what the nesting allows: a ring call
// inside the application's own section works, a packet that is delivered inside StartOutTransfer() is stored at once, and the
// function leaves no section open after its calls. The shim counts the depth: a leave at depth 0 aborts, and
// SimAssertNoCriticalSection() aborts when a section is still open.


namespace {


constexpr uint8 Interfaces[] = {
	0
};


constexpr uint8 Endpoints[] = {
	0x81,
	0x01
};


using VendorFunction = VendorBulkFunction<100, 256, 2>;


// Every byte is its position, so a byte that is lost, duplicated or moved shows up in the comparison.
void FillPattern(std::span<uint8> data) {
	for (std::size_t index = 0; index < data.size(); index++) {
		data[index] = static_cast<uint8>(index);
	}
}


struct Fixture {
	SimUSBDeviceAdapter sim;
	VendorFunction function;
	std::array<USBFunction*, 1> functions;
	USBDevice device;


	Fixture()
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


// A sim adapter that holds one host OUT packet while nothing is armed. StartOutTransfer() delivers the held packet from
// inside the call, so the completion runs inside the call, as the fsdev port does.
class HoldingSimAdapter : public SimUSBDeviceAdapter {
public:
	uint32 deliveredInCall = 0;


private:
	std::array<uint8, 64> held{};
	uint32 heldLength = 0;


public:
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
		// The port delivers inside the arm, so the section that Process() opened around the arm must still be open here.
		SIM_EXPECT(SimCriticalDepth() > 0);
		static_cast<void>(HostOut(address, std::span<const uint8>(held).first(length)));
		return status;
	}
};


// A sim adapter that takes each bulk IN transfer from inside StartInTransfer(), so the completion runs inside the call. The
// adapter contract says an IN completion follows the host's packet, so no port should do this. The core must still keep the
// IN queue moving when one does, and this adapter is the check for that.
class SyncInSimAdapter : public SimUSBDeviceAdapter {
public:
	uint32 takenInCall = 0;
	std::array<uint8, 256> taken{};
	uint32 takenCount = 0;


	ResultStatus StartInTransfer(uint8 address, std::span<const uint8> data, bool sendZeroLengthPacket) override {
		ResultStatus status = SimUSBDeviceAdapter::StartInTransfer(address, data, sendZeroLengthPacket);
		// Only the function's bulk IN is completed here. Control stages keep their normal completion.
		if (status != ResultStatus::ok || address != 0x81) {
			return status;
		}

		takenInCall++;
		std::array<uint8, 128> out = {};
		uint32 received = 0;
		SIM_EXPECT_EQ(HostIn(address, out, received), ResultStatus::ok);
		for (uint32 index = 0; index < received; index++) {
			taken[takenCount] = out[index];
			takenCount++;
		}

		return status;
	}
};


struct SyncInFixture {
	SyncInSimAdapter sim;
	VendorFunction function;
	std::array<USBFunction*, 1> functions;
	USBDevice device;


	SyncInFixture()
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


// The depth counts the sections the test opens itself, so nested sections come back to zero one leave at a time.
void TestSectionDepthCounts() {
	SIM_EXPECT_EQ(SimCriticalDepth(), 0);
	System::CriticalSection(true);
	System::CriticalSection(true);
	SIM_EXPECT_EQ(SimCriticalDepth(), 2);
	System::CriticalSection(false);
	SIM_EXPECT_EQ(SimCriticalDepth(), 1);
	System::CriticalSection(false);
	SIM_EXPECT_EQ(SimCriticalDepth(), 0);
	SimAssertNoCriticalSection();
}


// The application wraps Write(), Process() and Read() in its own section. The ring calls and the function's sections nest
// inside it, and the bytes come out in order.
void TestAppSectionAroundCalls() {
	Fixture fixture;
	fixture.Configure();

	std::array<uint8, 64> pattern = {};
	FillPattern(pattern);

	System::CriticalSection(true);
	SIM_EXPECT_EQ(fixture.function.Write(pattern), 64);
	fixture.device.Process();
	SIM_EXPECT_EQ(SimCriticalDepth(), 1);
	System::CriticalSection(false);
	SimAssertNoCriticalSection();

	std::array<uint8, 64> in = {};
	uint32 received = 0;
	SIM_EXPECT_EQ(fixture.sim.HostIn(0x81, in, received), ResultStatus::ok);
	SIM_EXPECT_EQ(received, 64);
	SIM_EXPECT(SimHost::SameBytes(in, pattern));

	std::array<uint8, 64> host = {};
	FillPattern(host);
	System::CriticalSection(true);
	Result<uint32> stored = fixture.sim.HostOut(0x01, host);
	SIM_EXPECT(stored.IsOk());
	SIM_EXPECT_EQ(stored.Value(), 64);
	// The OUT transfer is one packet, so the full packet above completed it. The zero-length packet is an empty transfer of its own.
	stored = fixture.sim.HostOut(0x01, std::span<const uint8>());
	SIM_EXPECT(stored.IsOk());
	SIM_EXPECT_EQ(stored.Value(), 0);
	SIM_EXPECT_EQ(fixture.function.Read(in), 64);
	System::CriticalSection(false);
	SimAssertNoCriticalSection();
	SIM_EXPECT(SimHost::SameBytes(in, host));
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


// The RX ring holds four OUT packets. The application reads one packet, and Process() arms the OUT endpoint: the packet the port
// held is delivered inside that call and stored at once, and the function leaves no section open after Process() returns.
void TestHeldPacketStoredInsideProcessSection() {
	HoldingFixture fixture;
	fixture.Configure();

	std::array<uint8, 256> pattern = {};
	FillPattern(pattern);
	for (std::size_t packet = 0; packet < 4; packet++) {
		SIM_EXPECT_EQ(fixture.sim.HostOut(0x01, std::span<const uint8>(pattern).subspan(packet * 64, 64)).Value(), 64U);
	}

	SIM_EXPECT(!fixture.sim.HasArmedTransfer(0x01));

	std::array<uint8, 10> packet = {};
	for (std::size_t index = 0; index < packet.size(); index++) {
		packet[index] = static_cast<uint8>(0xB0 + index);
	}

	SIM_EXPECT_EQ(fixture.sim.HostOut(0x01, packet).Error(), ResultStatus::notReady);
	fixture.sim.HoldPacket(packet);

	std::array<uint8, 64> first = {};
	SIM_EXPECT_EQ(fixture.function.Read(first), 64);

	// 256 - 64 read + 10 held is 202 bytes, and 54 bytes free are less than a packet, so the endpoint stays unarmed.
	fixture.device.Process();
	SIM_EXPECT_EQ(fixture.sim.deliveredInCall, 1U);
	SIM_EXPECT_EQ(SimCriticalDepth(), 0);
	SimAssertNoCriticalSection();
	SIM_EXPECT_EQ(fixture.function.GetReadableCount(), 192 + 10);
	SIM_EXPECT(!fixture.sim.HasArmedTransfer(0x01));

	std::array<uint8, 256> rest = {};
	SIM_EXPECT_EQ(fixture.function.Read(rest), 202);
	SIM_EXPECT(SimHost::SameBytes(std::span<const uint8>(rest).subspan(192, 10), std::span<const uint8>(packet)));
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


// A port that completes an IN transfer inside StartInTransfer() must not leave a filled stage idle. The first Process()
// starts a transfer that completes at once, and the second Process() must send the bytes written after it, in order.
void TestSynchronousInCompletionKeepsQueueMoving() {
	SyncInFixture fixture;
	fixture.Configure();

	std::array<uint8, 64> first = {};
	FillPattern(first);
	SIM_EXPECT_EQ(fixture.function.Write(first), 64);
	fixture.device.Process();
	SIM_EXPECT_EQ(fixture.sim.takenInCall, 1U);
	SimAssertNoCriticalSection();

	std::array<uint8, 64> second = {};
	for (std::size_t index = 0; index < second.size(); index++) {
		second[index] = static_cast<uint8>(0x80 + index);
	}

	SIM_EXPECT_EQ(fixture.function.Write(second), 64);
	fixture.device.Process();
	SIM_EXPECT_EQ(fixture.sim.takenInCall, 2U);
	SIM_EXPECT_EQ(fixture.sim.takenCount, 128U);
	SIM_EXPECT(SimHost::SameBytes(std::span<const uint8>(fixture.sim.taken).first(64), std::span<const uint8>(first)));
	SIM_EXPECT(SimHost::SameBytes(std::span<const uint8>(fixture.sim.taken).subspan(64, 64), std::span<const uint8>(second)));
	SimAssertNoCriticalSection();
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


}


int main() {
	TestSectionDepthCounts();
	TestAppSectionAroundCalls();
	TestHeldPacketStoredInsideProcessSection();
	TestSynchronousInCompletionKeepsQueueMoving();
	return SimTest::Finish("TestCriticalNesting");
}
