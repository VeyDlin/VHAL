#include <VHAL.h>
#include "DeviceFixture.h"
#include "Descriptors.h"
#include "SimAssert.h"
#include "SimHost.h"
#include <array>
#include <cstdio>
#include <span>


// State rules (USB 2.0 section 9.4): an interface, or an endpoint other than 0, does not exist until the device is configured.
// In the Default and Address states every request to one of them is a request error, so the core stalls it and no function
// sees it. In the Configured state the same requests are answered. Endpoint 0 requests are answered in every state.


namespace {


enum class Phase : uint8 {
	Default,
	Address,
	Configured,
	SuspendedFromDefault,
	SuspendedFromConfigured,
	// Configured, then SET_CONFIGURATION(0): the core deconfigures and returns to Address.
	DeconfiguredToAddress
};


// The reported request is the text of the SimHost::MakeSetup() call, so a failure names the request as the test wrote it.
#define SIM_EXPECT_REJECTED(fixture, phase, setup) ExpectRejected((fixture), (phase), (setup), #setup, __FILE__, __LINE__)


const char* PhaseName(Phase phase) {
	if (phase == Phase::Default) {
		return "Default";
	}

	if (phase == Phase::Address) {
		return "Address";
	}

	if (phase == Phase::SuspendedFromDefault) {
		return "SuspendedFromDefault";
	}

	if (phase == Phase::SuspendedFromConfigured) {
		return "SuspendedFromConfigured";
	}

	if (phase == Phase::DeconfiguredToAddress) {
		return "DeconfiguredToAddress";
	}

	return "Configured";
}


USBDeviceState StateOf(Phase phase) {
	if (phase == Phase::Default) {
		return USBDeviceState::Default;
	}

	if (phase == Phase::Address || phase == Phase::DeconfiguredToAddress) {
		return USBDeviceState::Address;
	}

	if (phase == Phase::SuspendedFromDefault || phase == Phase::SuspendedFromConfigured) {
		return USBDeviceState::Suspended;
	}

	return USBDeviceState::Configured;
}


USBDeviceAdapter::EndpointConfig Bulk(uint8 address) {
	USBDeviceAdapter::EndpointConfig config;
	config.address = address;
	config.type = USBEndpointType::Bulk;
	config.maxPacketSize = 64;
	config.doubleBuffer = false;
	return config;
}


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


// Runs one request and checks that the core rejected it itself: the host sees a STALL, the function was not called, no halt
// changed and the device state is the same. The request text and the phase are reported on failure.
void ExpectRejected(
	DeviceFixture& fixture,
	Phase phase,
	const USBSetupBytes& setup,
	const char* request,
	const char* file,
	int line)
{
	std::size_t requestsBefore = fixture.function.requests.count;
	std::size_t haltsBefore = fixture.function.haltChanges.count;
	USBDeviceState stateBefore = fixture.device.GetState();

	ResultStatus result = SimHost::Control(fixture.sim, setup);
	bool rejected = result == ResultStatus::accessError
		&& fixture.function.requests.count == requestsBefore
		&& fixture.function.haltChanges.count == haltsBefore
		&& fixture.device.GetState() == stateBefore;

	if (!rejected) {
		SimTest::failureCount++;
		std::fprintf(
			stderr,
			"%s:%d: %s in %s state was not rejected by the core (result %d)\n",
			file,
			line,
			request,
			PhaseName(phase),
			static_cast<int>(result)
		);
	}
}


void ReachPhase(DeviceFixture& fixture, Phase phase) {
	bool configured = phase == Phase::Configured || phase == Phase::SuspendedFromConfigured || phase == Phase::DeconfiguredToAddress;
	bool deconfigured = phase == Phase::DeconfiguredToAddress;
	bool addressed = configured || phase == Phase::Address;
	bool suspended = phase == Phase::SuspendedFromDefault || phase == Phase::SuspendedFromConfigured;

	fixture.sim.InjectBusReset();

	if (addressed) {
		SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x00, 5, 5, 0x0000, 0)), ResultStatus::ok);
	}

	if (configured) {
		SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x00, 9, 1, 0x0000, 0)), ResultStatus::ok);
	}

	if (deconfigured) {
		SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x00, 9, 0, 0x0000, 0)), ResultStatus::ok);
	}

	if (suspended) {
		fixture.sim.InjectSuspend();
	}

	SIM_EXPECT_EQ(fixture.device.GetState(), StateOf(phase));
}


// Every interface and non-zero endpoint request, standard, class and vendor, in both directions, against an owned interface,
// an owned endpoint and an unowned one. The owned endpoints 0x81 and 0x01 are opened, so they exist for the core: a request to
// them is rejected by the state rule alone. Without that rule the old core answers GET_STATUS, changes the halt and routes the
// class requests, which the checks below would catch.
void TestRequestsRejectedBeforeConfiguration(Phase phase) {
	DeviceFixture fixture;
	SIM_EXPECT_EQ(fixture.Start(), ResultStatus::ok);
	ReachPhase(fixture, phase);
	SIM_EXPECT_EQ(fixture.sim.OpenEndpoint(Bulk(0x81)), ResultStatus::ok);
	SIM_EXPECT_EQ(fixture.sim.OpenEndpoint(Bulk(0x01)), ResultStatus::ok);

	// Interface recipient: standard requests the core answers only when configured. The handler for GET_STATUS does not check
	// the state itself, so the first GET_STATUS line (owned interface 0) proves the rule by itself. The second names unowned
	// interface 5 and stalls with or without the rule.
	SIM_EXPECT_REJECTED(fixture, phase, SimHost::MakeSetup(0x81, 0, 0x0000, 0x0000, 2));
	SIM_EXPECT_REJECTED(fixture, phase, SimHost::MakeSetup(0x81, 0, 0x0000, 0x0005, 2));
	SIM_EXPECT_REJECTED(fixture, phase, SimHost::MakeSetup(0x01, 1, 0x0000, 0x0000, 0));
	SIM_EXPECT_REJECTED(fixture, phase, SimHost::MakeSetup(0x01, 3, 0x0001, 0x0000, 0));
	// GET_INTERFACE and SET_INTERFACE refuse these states on their own, so the two requests below do not prove the new rule.
	SIM_EXPECT_REJECTED(fixture, phase, SimHost::MakeSetup(0x81, 10, 0x0000, 0x0000, 1));
	SIM_EXPECT_REJECTED(fixture, phase, SimHost::MakeSetup(0x01, 11, 0x0000, 0x0000, 0));

	// Interface recipient: class, vendor and unhandled requests, which the core would route to the owner.
	SIM_EXPECT_REJECTED(fixture, phase, SimHost::MakeSetup(0xA1, TestFunction::ClassInRequest, 0x0000, 0x0000, 4));
	SIM_EXPECT_REJECTED(fixture, phase, SimHost::MakeSetup(0x21, TestFunction::ClassOutRequest, 0x0000, 0x0000, 2));
	SIM_EXPECT_REJECTED(fixture, phase, SimHost::MakeSetup(0x21, TestFunction::ClassNoDataRequest, 0x0000, 0x0000, 0));
	SIM_EXPECT_REJECTED(fixture, phase, SimHost::MakeSetup(0xC1, TestFunction::ClassInRequest, 0x0000, 0x0000, 4));
	SIM_EXPECT_REJECTED(fixture, phase, SimHost::MakeSetup(0x81, TestFunction::UnhandledRequest, 0x0000, 0x0000, 2));

	// Endpoint recipient, owned endpoints 0x81 and 0x01: GET_STATUS, SET/CLEAR_FEATURE(ENDPOINT_HALT) and class requests.
	SIM_EXPECT_REJECTED(fixture, phase, SimHost::MakeSetup(0x82, 0, 0x0000, 0x0081, 2));
	SIM_EXPECT_REJECTED(fixture, phase, SimHost::MakeSetup(0x82, 0, 0x0000, 0x0001, 2));
	SIM_EXPECT_REJECTED(fixture, phase, SimHost::MakeSetup(0x02, 3, 0x0000, 0x0081, 0));
	SIM_EXPECT_REJECTED(fixture, phase, SimHost::MakeSetup(0x02, 1, 0x0000, 0x0081, 0));
	SIM_EXPECT_REJECTED(fixture, phase, SimHost::MakeSetup(0x02, 3, 0x0000, 0x0001, 0));
	SIM_EXPECT_REJECTED(fixture, phase, SimHost::MakeSetup(0xA2, TestFunction::ClassInRequest, 0x0000, 0x0081, 4));
	SIM_EXPECT_REJECTED(fixture, phase, SimHost::MakeSetup(0x22, TestFunction::ClassOutRequest, 0x0000, 0x0001, 2));

	// Endpoint recipient, unowned endpoint 0x82: rejected in these states as well.
	SIM_EXPECT_REJECTED(fixture, phase, SimHost::MakeSetup(0x82, 0, 0x0000, 0x0082, 2));
	SIM_EXPECT_REJECTED(fixture, phase, SimHost::MakeSetup(0x02, 3, 0x0000, 0x0082, 0));

	// Nothing reached the function, no halt was made, and the device is still in the same state.
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
	SIM_EXPECT(!fixture.sim.IsStalled(0x81));
	SIM_EXPECT_EQ(fixture.sim.GetStallCount(0x81), 0);
	SIM_EXPECT_EQ(fixture.device.GetState(), StateOf(phase));
	SIM_EXPECT(!fixture.function.AnyOverflow());
}


// Endpoint 0 requests are answered in every state: GET_STATUS, and SET/CLEAR_FEATURE(ENDPOINT_HALT) is acknowledged with no
// effect. The halt is never reported to a function, because endpoint 0 belongs to the core.
void TestEndpointZeroAnsweredInEveryState(Phase phase) {
	DeviceFixture fixture;
	SIM_EXPECT_EQ(fixture.Start(), ResultStatus::ok);
	ReachPhase(fixture, phase);

	ExpectIn(fixture.sim, SimHost::MakeSetup(0x82, 0, 0x0000, 0x0080, 2), std::array<uint8, 2>{ 0x00, 0x00 });
	ExpectIn(fixture.sim, SimHost::MakeSetup(0x82, 0, 0x0000, 0x0000, 2), std::array<uint8, 2>{ 0x00, 0x00 });
	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x02, 3, 0x0000, 0x0080, 0)), ResultStatus::ok);
	SIM_EXPECT(!fixture.sim.IsStalled(0x80));
	SIM_EXPECT_EQ(fixture.sim.GetStallCount(0x80), 0);
	ExpectIn(fixture.sim, SimHost::MakeSetup(0x82, 0, 0x0000, 0x0080, 2), std::array<uint8, 2>{ 0x00, 0x00 });
	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x02, 1, 0x0000, 0x0000, 0)), ResultStatus::ok);
	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x02, 3, 0x0000, 0x0000, 0)), ResultStatus::ok);
	SIM_EXPECT(!fixture.sim.IsStalled(0x80));

	SIM_EXPECT_EQ(fixture.function.haltChanges.count, 0);
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
	SIM_EXPECT_EQ(fixture.device.GetState(), StateOf(phase));
}


// The same requests that are rejected before configuration are answered once configured, with the answers of the stack.
void TestInterfaceAndEndpointAnsweredWhenConfigured() {
	DeviceFixture fixture;
	SIM_EXPECT_EQ(fixture.Start(), ResultStatus::ok);
	ReachPhase(fixture, Phase::Configured);

	ExpectIn(fixture.sim, SimHost::MakeSetup(0x81, 0, 0x0000, 0x0000, 2), std::array<uint8, 2>{ 0x00, 0x00 });
	ExpectIn(fixture.sim, SimHost::MakeSetup(0x81, 10, 0x0000, 0x0000, 1), std::array<uint8, 1>{ 0x00 });
	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x01, 11, 0x0000, 0x0000, 0)), ResultStatus::ok);
	ExpectIn(fixture.sim, SimHost::MakeSetup(0xA1, TestFunction::ClassInRequest, 0x0000, 0x0000, 4), std::array<uint8, 4>{ 0xDE, 0xAD, 0xBE, 0xEF });
	ExpectIn(fixture.sim, SimHost::MakeSetup(0xC1, TestFunction::ClassInRequest, 0x0000, 0x0000, 4), std::array<uint8, 4>{ 0xDE, 0xAD, 0xBE, 0xEF });
	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x21, TestFunction::ClassNoDataRequest, 0x0000, 0x0000, 0)), ResultStatus::ok);

	std::array<uint8, 2> hostData = { 0x12, 0x34 };
	std::array<uint8, 4> in = {};
	uint32 received = 0;
	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x21, TestFunction::ClassOutRequest, 0x0000, 0x0000, 2), hostData, in, received), ResultStatus::ok);
	SIM_EXPECT(SimHost::SameBytes(fixture.function.controlOutData.View(), hostData));

	// The endpoint 0x81 exists now: its status follows the halt, and the halt is reported to the owner.
	ExpectIn(fixture.sim, SimHost::MakeSetup(0x82, 0, 0x0000, 0x0081, 2), std::array<uint8, 2>{ 0x00, 0x00 });
	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x02, 3, 0x0000, 0x0081, 0)), ResultStatus::ok);
	SIM_EXPECT(fixture.sim.IsStalled(0x81));
	ExpectIn(fixture.sim, SimHost::MakeSetup(0x82, 0, 0x0000, 0x0081, 2), std::array<uint8, 2>{ 0x01, 0x00 });
	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x02, 1, 0x0000, 0x0081, 0)), ResultStatus::ok);
	SIM_EXPECT(!fixture.sim.IsStalled(0x81));
	SIM_EXPECT_EQ(fixture.function.haltChanges.count, 2);
	SIM_EXPECT(fixture.function.haltChanges.Back().address == 0x81 && !fixture.function.haltChanges.Back().halted);
	ExpectIn(fixture.sim, SimHost::MakeSetup(0xA2, TestFunction::ClassInRequest, 0x0000, 0x0081, 4), std::array<uint8, 4>{ 0xDE, 0xAD, 0xBE, 0xEF });

	// Unowned endpoint and unowned interface still stall once configured.
	ExpectStall(fixture.sim, SimHost::MakeSetup(0x82, 0, 0x0000, 0x0082, 2));
	ExpectStall(fixture.sim, SimHost::MakeSetup(0x81, 0, 0x0000, 0x0005, 2));

	SIM_EXPECT_EQ(fixture.device.GetState(), USBDeviceState::Configured);
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
	SIM_EXPECT(!fixture.function.AnyOverflow());
}


// A device suspended from Configured keeps its configuration, so its class requests to the interface and its GET_STATUS for
// the interface are answered while suspended (the standard requests answered while suspended are in TestControlStages).
// After the resume the device is Configured again.
void TestSuspendedFromConfiguredAnswersClassRequests() {
	DeviceFixture fixture;
	SIM_EXPECT_EQ(fixture.Start(), ResultStatus::ok);
	ReachPhase(fixture, Phase::SuspendedFromConfigured);

	ExpectIn(fixture.sim, SimHost::MakeSetup(0xA1, TestFunction::ClassInRequest, 0x0000, 0x0000, 4), std::array<uint8, 4>{ 0xDE, 0xAD, 0xBE, 0xEF });
	ExpectIn(fixture.sim, SimHost::MakeSetup(0x81, 0, 0x0000, 0x0000, 2), std::array<uint8, 2>{ 0x00, 0x00 });

	fixture.sim.InjectResume();
	SIM_EXPECT_EQ(fixture.device.GetState(), USBDeviceState::Configured);
	ExpectIn(fixture.sim, SimHost::MakeSetup(0x81, 10, 0x0000, 0x0000, 1), std::array<uint8, 1>{ 0x00 });
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


}


int main() {
	TestRequestsRejectedBeforeConfiguration(Phase::Default);
	TestRequestsRejectedBeforeConfiguration(Phase::Address);
	TestRequestsRejectedBeforeConfiguration(Phase::SuspendedFromDefault);
	TestRequestsRejectedBeforeConfiguration(Phase::DeconfiguredToAddress);
	TestEndpointZeroAnsweredInEveryState(Phase::Default);
	TestEndpointZeroAnsweredInEveryState(Phase::Address);
	TestEndpointZeroAnsweredInEveryState(Phase::Configured);
	TestInterfaceAndEndpointAnsweredWhenConfigured();
	TestSuspendedFromConfiguredAnswersClassRequests();
	return SimTest::Finish("TestStateRules");
}
