#include <VHAL.h>
#include "DeviceFixture.h"
#include "Descriptors.h"
#include "SimAssert.h"
#include "SimHost.h"
#include <array>
#include <span>

// Routing: class and vendor requests reach the function that owns their interface or endpoint, a request nobody owns
// stalls, and each bulk completion reaches its function while endpoint 0 completions stay with the core.


namespace {


void ExpectIn(SimUSBDeviceAdapter& sim, const USBSetupBytes& setup, std::span<const uint8> expected) {
	std::array<uint8, 256> in = {};
	uint32 received = 0;
	SIM_EXPECT_EQ(SimHost::Control(sim, setup, std::span<const uint8>(), in, received), ResultStatus::ok);
	SIM_EXPECT(SimHost::SameBytes(std::span<const uint8>(in).first(received), expected));
}


void ExpectStall(SimUSBDeviceAdapter& sim, const USBSetupBytes& setup) {
	std::array<uint8, 256> in = {};
	uint32 received = 0;
	SIM_EXPECT_EQ(SimHost::Control(sim, setup, std::span<const uint8>(), in, received), ResultStatus::accessError);
}


void ConfigureTo(DeviceFixture& fixture) {
	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x00, 5, 5, 0x0000, 0)), ResultStatus::ok);
	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x00, 9, 1, 0x0000, 0)), ResultStatus::ok);
}


void TestClassAndVendorRequestsRouted() {
	DeviceFixture fixture;
	SIM_EXPECT_EQ(fixture.Start(), ResultStatus::ok);
	fixture.sim.InjectBusReset();
	ConfigureTo(fixture);

	// Class IN to interface 0: the function answers with its own bytes.
	ExpectIn(fixture.sim, SimHost::MakeSetup(0xA1, TestFunction::ClassInRequest, 0x0000, 0x0000, 4), std::array<uint8, 4>{ 0xDE, 0xAD, 0xBE, 0xEF });
	SIM_EXPECT_EQ(fixture.function.requests.Back(), TestFunction::ClassInRequest);

	// Class OUT to interface 0: the host's two bytes reach the function through ControlReceiveData.
	std::array<uint8, 2> hostData = { 0x12, 0x34 };
	std::array<uint8, 4> in = {};
	uint32 received = 0;
	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x21, TestFunction::ClassOutRequest, 0x0000, 0x0000, 2), hostData, in, received), ResultStatus::ok);
	SIM_EXPECT(SimHost::SameBytes(fixture.function.controlOutData.View(), hostData));

	// Class request with no data stage: the core completes it with a status stage.
	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x21, TestFunction::ClassNoDataRequest, 0x0000, 0x0000, 0)), ResultStatus::ok);

	// A class IN that asks for no data cannot take an answer: the function's ControlSendData is invalidParameter, so it stalls.
	ExpectStall(fixture.sim, SimHost::MakeSetup(0xA1, TestFunction::ClassInRequest, 0x0000, 0x0000, 0));

	// Vendor requests to the interface go to the owner as well.
	ExpectIn(fixture.sim, SimHost::MakeSetup(0xC1, TestFunction::ClassInRequest, 0x0000, 0x0000, 4), std::array<uint8, 4>{ 0xDE, 0xAD, 0xBE, 0xEF });

	// A request the function does not handle stalls, here a standard request to the interface that is not answered.
	ExpectStall(fixture.sim, SimHost::MakeSetup(0x81, TestFunction::UnhandledRequest, 0x0000, 0x0000, 2));

	// An endpoint request is routed to the owner of that endpoint.
	ExpectIn(fixture.sim, SimHost::MakeSetup(0xA2, TestFunction::ClassInRequest, 0x0000, 0x0081, 4), std::array<uint8, 4>{ 0xDE, 0xAD, 0xBE, 0xEF });
	ExpectStall(fixture.sim, SimHost::MakeSetup(0xA2, TestFunction::ClassInRequest, 0x0000, 0x0082, 4));

	// Interface 5 has no owner, and class or vendor requests to the device itself stall.
	ExpectStall(fixture.sim, SimHost::MakeSetup(0xA1, TestFunction::ClassInRequest, 0x0000, 0x0005, 4));
	ExpectStall(fixture.sim, SimHost::MakeSetup(0xA0, TestFunction::ClassInRequest, 0x0000, 0x0000, 4));
	ExpectStall(fixture.sim, SimHost::MakeSetup(0x40, 0x42, 0x0000, 0x0000, 0));
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
	SIM_EXPECT(!fixture.function.AnyOverflow());
}


void TestBulkCompletionsRouted() {
	DeviceFixture fixture;
	SIM_EXPECT_EQ(fixture.Start(), ResultStatus::ok);
	fixture.sim.InjectBusReset();
	ConfigureTo(fixture);

	// The control transfers above complete on endpoint 0 and never reach the function's completion list.
	std::size_t controlCompletions = fixture.function.completions.count;

	// The OUT completion carries the bytes stored, and the function re-arms the buffer.
	std::array<uint8, 4> outPacket = { 1, 2, 3, 4 };
	SIM_EXPECT(fixture.sim.HostOut(0x01, outPacket).IsOk());
	SIM_EXPECT_EQ(fixture.function.completions.count, controlCompletions + 1);
	SIM_EXPECT_EQ(fixture.function.completions.Back().address, 0x01);
	SIM_EXPECT_EQ(fixture.function.completions.Back().byteCount, 4);
	SIM_EXPECT(SimHost::SameBytes(fixture.function.receivedOut.View(), outPacket));
	SIM_EXPECT(fixture.sim.HasArmedTransfer(0x01));

	// A zero-length OUT packet is no data, and it completes with 0 and re-arms the same way.
	SIM_EXPECT(fixture.sim.HostOut(0x01, std::span<const uint8>()).IsOk());
	SIM_EXPECT_EQ(fixture.function.completions.Back().byteCount, 0);
	SIM_EXPECT(fixture.sim.HasArmedTransfer(0x01));

	// The function queues data, Process() arms the IN endpoint, and the host's read completes it at the function.
	std::array<uint8, 3> queued = { 5, 6, 7 };
	fixture.function.QueueIn(queued);
	fixture.device.Process();
	SIM_EXPECT(fixture.sim.HasArmedTransfer(0x81));
	std::array<uint8, 64> in = {};
	uint32 received = 0;
	SIM_EXPECT_EQ(fixture.sim.HostIn(0x81, in, received), ResultStatus::ok);
	SIM_EXPECT(SimHost::SameBytes(std::span<const uint8>(in).first(received), queued));
	SIM_EXPECT_EQ(fixture.function.completions.Back().address, 0x81);
	SIM_EXPECT_EQ(fixture.function.completions.Back().byteCount, 3);
	SIM_EXPECT(!fixture.sim.HasArmedTransfer(0x81));
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
	SIM_EXPECT(!fixture.function.AnyOverflow());
}


}


int main() {
	TestClassAndVendorRequestsRouted();
	TestBulkCompletionsRouted();
	return SimTest::Finish("TestRouting");
}
