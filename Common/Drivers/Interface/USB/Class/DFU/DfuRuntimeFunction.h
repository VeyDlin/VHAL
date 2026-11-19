#pragma once
#include <array>
#include <Drivers/Interface/USB/Class/DFU/USBDfuProtocol.h>
#include <Drivers/Interface/USB/Device/USBFunction.h>
#include <System/System.h>
#include <Utilities/VSTD/utility/Delegate.h>


// The run-time function of a DFU device: the interface a running application exposes so a host can ask it to restart into its
// DFU loader (DFU 1.1 section 5). It answers DFU_DETACH, DFU_GETSTATUS and DFU_GETSTATE, and nothing else.
//
// DFU_DETACH moves the state to appDETACH and starts the detach timer. The timer is wTimeout, or wDetachTimeOut when wTimeout is 0
// or larger than it. The detach delegate is called from Process(), in thread context, with the bitWillDetach bit of the descriptor.
// The application then either restarts its device into the loader (no WillDetach: the host's USB reset comes next and the loader
// enumerates in DFU mode), or restarts the device itself when WillDetach is set. The restart is an MCU reset: USBDevice::Stop() and
// Start() present the same run-time descriptors again.
//
// A reset that comes inside the timer keeps the request: the delegate is called from the next Process(), even when that runs after
// the timer. Without a reset inside the timer, Process() returns the state to appIDLE and the delegate is not called, as the
// specification asks; the application is not restarted for a host that did not follow up.
//
// Interrupt context: OnControlRequest and OnBusReset. Thread context: Process() and the delegate. The detach state is set in the
// interrupt and read in Process() inside a short critical section.
class DfuRuntimeFunction : public USBFunction {
public:
	// The application's reaction to DFU_DETACH: restart into the loader, or detach itself when willDetach is true.
	using DetachDelegate = VSTD::Delegate<void(bool willDetach)>;


private:
	DetachDelegate detachDelegate;
	USBDfuFunctional functional;
	DfuState state = DfuState::appIDLE;
	uint64 detachDeadline = 0;
	bool detachRequested = false;
	std::array<uint8, 6> statusResponse{};
	std::array<uint8, 1> stateResponse{};


public:
	// The layout of the run-time interface (USBFunctionLayout from the fluent builder).
	explicit DfuRuntimeFunction(const USBFunctionLayout& layout)
		: USBFunction(layout),
		  functional(ParseUSBDfuFunctional(layout.descriptors)) {
	}


	// Set before the device starts: the delegate is not guarded against a call that is already running.
	void SetDetachDelegate(DetachDelegate delegate) {
		detachDelegate = delegate;
	}


	DfuState GetState() const {
		return state;
	}


	ResultStatus OnConfigured(USBDeviceContext&) override {
		if (!functional.found) {
			return ResultStatus::invalidParameter;
		}

		state = DfuState::appIDLE;
		detachRequested = false;
		return ResultStatus::ok;
	}


	// The host reset that answers DFU_DETACH. Inside the timer the request stays pending, so Process() still calls the delegate when it
	// runs late. After the timer the request is dropped, as the device has already gone back to appIDLE.
	void OnBusReset() override {
		if (state == DfuState::appDETACH && !WillDetach() && System::GetTick() >= detachDeadline) {
			detachRequested = false;
		}

		state = DfuState::appIDLE;
	}


	ResultStatus OnControlRequest(const USBSetupPacket& setup, USBDeviceContext& context) override {
		if (setup.type != USBRequestType::Class) {
			return ResultStatus::notSupported;
		}

		if (setup.request == static_cast<uint8>(DfuRequest::Detach)) {
			return HandleDetach(setup);
		}

		if (setup.request == static_cast<uint8>(DfuRequest::GetStatus) && setup.direction == USBDirection::In) {
			statusResponse = MakeUSBDfuStatusResponse(DfuStatus::OK, 0, state);
			context.ControlSendData(statusResponse);
			return ResultStatus::ok;
		}

		if (setup.request == static_cast<uint8>(DfuRequest::GetState) && setup.direction == USBDirection::In) {
			stateResponse[0] = static_cast<uint8>(state);
			context.ControlSendData(stateResponse);
			return ResultStatus::ok;
		}

		return ResultStatus::notSupported;
	}


	// Deferred work: delivers a detach request to the application and ends the detach timer when it runs out.
	void Process(USBDeviceContext&) override {
		bool willDetach = WillDetach();
		bool requested = false;
		bool expired = false;

		System::CriticalSection(true);
		requested = detachRequested;
		detachRequested = false;
		if (state == DfuState::appDETACH && !willDetach && System::GetTick() >= detachDeadline) {
			state = DfuState::appIDLE;
			expired = true;
		}

		System::CriticalSection(false);

		if (requested && !expired && detachDelegate) {
			detachDelegate(willDetach);
		}
	}


private:
	bool WillDetach() const {
		return (functional.attributes & DfuAttributeWillDetach) != 0;
	}


	// DFU_DETACH is an OUT request without data. It is answered only in appIDLE: in appDETACH a second detach is a stall. wTimeout 0
	// asks for the descriptor's timeout, as does a wTimeout above it.
	ResultStatus HandleDetach(const USBSetupPacket& setup) {
		if (setup.direction != USBDirection::Out || setup.length != 0 || state != DfuState::appIDLE) {
			return ResultStatus::notSupported;
		}

		uint16 wait = (setup.value == 0 || setup.value > functional.detachTimeoutMs) ? functional.detachTimeoutMs : setup.value;
		state = DfuState::appDETACH;
		detachDeadline = System::GetTick() + wait;
		detachRequested = true;
		return ResultStatus::ok;
	}
};
