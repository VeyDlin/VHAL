#include <VHAL.h>
#include "DfuTestDevices.h"
#include "SimAssert.h"
#include "SimHost.h"
#include <Drivers/Interface/USB/Class/DFU/DfuRuntimeFunction.h>
#include <Drivers/Interface/USB/Class/DFU/USBDfuBuilder.h>
#include <Drivers/Interface/USB/Descriptor/Builder/USBDescriptors.h>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>


// The run-time DFU function through the simulated controller and the host: DFU_DETACH reaches the application's delegate from
// Process(), the device answers appIDLE or appDETACH, the detach timeout ends the detach when no reset comes, and WillDetach
// makes the device ask for its own detach-attach.


using namespace DfuTest;


namespace {


constexpr uint8 RequestDetach = static_cast<uint8>(DfuRequest::Detach);
constexpr uint8 RequestDnload = static_cast<uint8>(DfuRequest::Dnload);
constexpr uint8 RequestGetStatus = static_cast<uint8>(DfuRequest::GetStatus);
constexpr uint8 RequestGetState = static_cast<uint8>(DfuRequest::GetState);


// Wait for the detach timer in the tests: the shim's clock is the steady clock, so a test sleeps past the timeout.
void Sleep(uint32 milliseconds) {
	std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}


// Run-time device with a 20 ms detach timeout, so a test can let it run out.
using RuntimeShortUsb = USBDescriptors<+[] {
	return USBDeviceBuilder()
		.VendorId(0x1209)
		.ProductId(0x0003)
		.DeviceRelease(0x0100)
		.Manufacturer("Sim")
		.Product("DFU runtime short")
		.SerialNumber("1")
		.Configuration(USBConfigurationBuilder()
			.MaxPowerMilliamps(100)
			.Function(USBDfuBuilder()
				.Runtime()
				.DetachTimeoutMs(20)
				.TransferSize(64)));
}>;


uint32 detachCalls = 0;
bool lastWillDetach = false;


void OnDetach(bool willDetach) {
	detachCalls++;
	lastWillDetach = willDetach;
}


// A device context whose control answers can be made to fail. The other calls are not used by DFU and answer notSupported.
class FailingSendContext : public USBDeviceContext {
public:
	bool sendFails = false;


	ResultStatus OpenEndpoint(const USBDeviceAdapter::EndpointConfig&) override {
		return ResultStatus::notSupported;
	}


	ResultStatus CloseEndpoint(uint8) override {
		return ResultStatus::notSupported;
	}


	ResultStatus StartInTransfer(uint8, std::span<const uint8>, bool) override {
		return ResultStatus::notSupported;
	}


	ResultStatus StartOutTransfer(uint8, std::span<uint8>) override {
		return ResultStatus::notSupported;
	}


	ResultStatus Stall(uint8) override {
		return ResultStatus::notSupported;
	}


	ResultStatus ClearStall(uint8) override {
		return ResultStatus::notSupported;
	}


	ResultStatus RemoteWakeup() override {
		return ResultStatus::notSupported;
	}


	ResultStatus ControlSendData(std::span<const uint8>) override {
		return sendFails ? ResultStatus::notSupported : ResultStatus::ok;
	}


	ResultStatus ControlReceiveData(std::span<uint8>) override {
		return ResultStatus::notSupported;
	}
};


void CheckDetachReachesDelegateAndAnswersAppDetach() {
	RuntimeDevice<RuntimeUsb> fixture;
	fixture.Configure();
	fixture.function.SetDetachDelegate(DfuRuntimeFunction::DetachDelegate::Bind<&OnDetach>());
	detachCalls = 0;
	SIM_EXPECT_EQ(ReadState(fixture.sim), DfuState::appIDLE);

	// wValue is the host's wTimeout. The request is answered before the application is told: the delegate runs from Process().
	SIM_EXPECT_EQ(SendNoData(fixture.sim, RequestDetach, 100), ResultStatus::ok);
	SIM_EXPECT_EQ(ReadState(fixture.sim), DfuState::appDETACH);
	StatusAnswer status = ReadStatus(fixture.sim);
	SIM_EXPECT(status.valid);
	SIM_EXPECT_EQ(status.state, DfuState::appDETACH);
	SIM_EXPECT_EQ(status.status, DfuStatus::OK);
	SIM_EXPECT_EQ(status.pollTimeoutMs, 0);
	SIM_EXPECT_EQ(detachCalls, 0);

	fixture.device.Process();
	SIM_EXPECT_EQ(detachCalls, 1);
	SIM_EXPECT(!lastWillDetach);

	// The delegate is called once per request.
	fixture.device.Process();
	SIM_EXPECT_EQ(detachCalls, 1);
}


void CheckDetachTimeoutReturnsToAppIdle() {
	RuntimeDevice<RuntimeShortUsb> fixture;
	fixture.Configure();
	fixture.function.SetDetachDelegate(DfuRuntimeFunction::DetachDelegate::Bind<&OnDetach>());
	detachCalls = 0;

	SIM_EXPECT_EQ(SendNoData(fixture.sim, RequestDetach, 10), ResultStatus::ok);
	Sleep(60);

	// No reset came in time: the timer ends in appIDLE, and the application is not asked to restart.
	fixture.device.Process();
	SIM_EXPECT_EQ(fixture.function.GetState(), DfuState::appIDLE);
	SIM_EXPECT_EQ(ReadState(fixture.sim), DfuState::appIDLE);
	SIM_EXPECT_EQ(detachCalls, 0);
}


void CheckHostTimeoutIsClampedToDescriptor() {
	RuntimeDevice<RuntimeShortUsb> fixture;
	fixture.Configure();

	// The host asks for 60 s; the descriptor's 20 ms limit wins, so the device is back in appIDLE after 60 ms.
	SIM_EXPECT_EQ(SendNoData(fixture.sim, RequestDetach, 60000), ResultStatus::ok);
	Sleep(60);
	fixture.device.Process();
	SIM_EXPECT_EQ(fixture.function.GetState(), DfuState::appIDLE);
}


void CheckWillDetachAsksForDetachAndStays() {
	RuntimeDevice<RuntimeWillDetachUsb> fixture;
	fixture.Configure();
	fixture.function.SetDetachDelegate(DfuRuntimeFunction::DetachDelegate::Bind<&OnDetach>());
	detachCalls = 0;

	SIM_EXPECT_EQ(SendNoData(fixture.sim, RequestDetach, 0), ResultStatus::ok);
	fixture.device.Process();
	SIM_EXPECT_EQ(detachCalls, 1);
	SIM_EXPECT(lastWillDetach);

	// The device detaches itself, so there is no timer: appDETACH stays until the application detaches it.
	Sleep(30);
	fixture.device.Process();
	SIM_EXPECT_EQ(fixture.function.GetState(), DfuState::appDETACH);
	SIM_EXPECT_EQ(detachCalls, 1);
}


void CheckSecondDetachAndUnknownRequestsStall() {
	RuntimeDevice<RuntimeUsb> fixture;
	fixture.Configure();
	SIM_EXPECT_EQ(SendNoData(fixture.sim, RequestDetach, 100), ResultStatus::ok);

	// In appDETACH a second detach, and any other class request, is a stall. The state stays appDETACH.
	SIM_EXPECT_EQ(SendNoData(fixture.sim, RequestDetach, 100), ResultStatus::accessError);
	SIM_EXPECT_EQ(ReadState(fixture.sim), DfuState::appDETACH);

	RuntimeDevice<RuntimeUsb> idle;
	idle.Configure();
	std::vector<uint8> block(64, 0);
	SIM_EXPECT_EQ(SendOut(idle.sim, RequestDnload, 0, block), ResultStatus::accessError);
	SIM_EXPECT_EQ(ReadState(idle.sim), DfuState::appIDLE);
}


void CheckGetStatusIsAnsweredInAppIdle() {
	RuntimeDevice<RuntimeUsb> fixture;
	fixture.Configure();
	StatusAnswer status = ReadStatus(fixture.sim);
	SIM_EXPECT(status.valid);
	SIM_EXPECT_EQ(status.status, DfuStatus::OK);
	SIM_EXPECT_EQ(status.state, DfuState::appIDLE);
	SIM_EXPECT_EQ(status.pollTimeoutMs, 0);
}


void CheckZeroTimeoutUsesDescriptor() {
	RuntimeDevice<RuntimeShortUsb> fixture;
	fixture.Configure();
	fixture.function.SetDetachDelegate(DfuRuntimeFunction::DetachDelegate::Bind<&OnDetach>());
	detachCalls = 0;

	// wTimeout 0 asks for the descriptor's 20 ms, so the request is still pending when Process() runs at once.
	SIM_EXPECT_EQ(SendNoData(fixture.sim, RequestDetach, 0), ResultStatus::ok);
	fixture.device.Process();
	SIM_EXPECT_EQ(detachCalls, 1);
	SIM_EXPECT_EQ(fixture.function.GetState(), DfuState::appDETACH);
}


void CheckResetInsideTimeoutDeliversLateProcess() {
	RuntimeDevice<RuntimeShortUsb> fixture;
	fixture.Configure();
	fixture.function.SetDetachDelegate(DfuRuntimeFunction::DetachDelegate::Bind<&OnDetach>());
	detachCalls = 0;

	// The host resets at once, and the application's main loop runs only after the 20 ms timer. The reset came in time, so the
	// request still reaches the application.
	SIM_EXPECT_EQ(SendNoData(fixture.sim, RequestDetach, 100), ResultStatus::ok);
	fixture.sim.InjectBusReset();
	Sleep(60);
	fixture.device.Process();
	SIM_EXPECT_EQ(detachCalls, 1);
	SIM_EXPECT(!lastWillDetach);
	SIM_EXPECT_EQ(fixture.function.GetState(), DfuState::appIDLE);
}


void CheckResetAfterTimeoutDropsRequest() {
	RuntimeDevice<RuntimeShortUsb> fixture;
	fixture.Configure();
	fixture.function.SetDetachDelegate(DfuRuntimeFunction::DetachDelegate::Bind<&OnDetach>());
	detachCalls = 0;

	// The reset comes after the timer: the device has gone back to appIDLE, so the application is not asked to restart.
	SIM_EXPECT_EQ(SendNoData(fixture.sim, RequestDetach, 10), ResultStatus::ok);
	Sleep(60);
	fixture.sim.InjectBusReset();
	fixture.device.Process();
	SIM_EXPECT_EQ(detachCalls, 0);
	SIM_EXPECT_EQ(fixture.function.GetState(), DfuState::appIDLE);
}


// A status or state answer the core cannot send is returned as the request's result, so the core stalls the request.
void CheckFailedAnswerStalls() {
	DfuRuntimeFunction function(RuntimeUsb::Function(0));
	FailingSendContext context;
	context.sendFails = true;
	SIM_EXPECT_EQ(function.OnConfigured(context), ResultStatus::ok);

	SIM_EXPECT_EQ(function.OnControlRequest(USBSetupPacket::Parse(SimHost::MakeSetup(0xA1, RequestGetStatus, 0, 0, 6)), context), ResultStatus::notSupported);
	SIM_EXPECT_EQ(function.OnControlRequest(USBSetupPacket::Parse(SimHost::MakeSetup(0xA1, RequestGetState, 0, 0, 1)), context), ResultStatus::notSupported);
	SIM_EXPECT(function.GetState() == DfuState::appIDLE);
}


}


int main() {
	CheckDetachReachesDelegateAndAnswersAppDetach();
	CheckDetachTimeoutReturnsToAppIdle();
	CheckHostTimeoutIsClampedToDescriptor();
	CheckWillDetachAsksForDetachAndStays();
	CheckSecondDetachAndUnknownRequestsStall();
	CheckGetStatusIsAnsweredInAppIdle();
	CheckZeroTimeoutUsesDescriptor();
	CheckResetInsideTimeoutDeliversLateProcess();
	CheckResetAfterTimeoutDropsRequest();
	CheckFailedAnswerStalls();
	return SimTest::Finish("TestDfuRuntime");
}
