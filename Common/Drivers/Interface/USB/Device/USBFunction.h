#pragma once
#include <Adapter/USBDeviceAdapter.h>
#include <Drivers/Interface/USB/USBProtocol.h>
#include <System/System.h>
#include <span>


// What a function may ask of the device core. The core implements it and passes it to every callback that may use it.
// A function keeps no pointer to it.
//
// Where the calls may be made:
//   - From the callbacks of USBFunction: they run in interrupt context, where the calls need no locking.
//   - From Process(): thread context. A call that starts or stops a transfer can overlap the interrupt that completes the
//     same endpoint, so the function wraps such a call in System::CriticalSection(true) and (false). Sections nest, so a
//     call made inside the function's own section is safe, and the section stays short.
class USBDeviceContext {
public:
	virtual ~USBDeviceContext() = default;


	// Endpoint and transfer calls, forwarded to the adapter. They return the adapter's status (see USBDeviceAdapter). A
	// successful Stall() or ClearStall() on a non-control endpoint also runs OnEndpointHaltChanged() of its owner.
	virtual ResultStatus OpenEndpoint(const USBDeviceAdapter::EndpointConfig& config) = 0;

	virtual ResultStatus CloseEndpoint(uint8 address) = 0;

	virtual ResultStatus StartInTransfer(uint8 address, std::span<const uint8> data, bool sendZeroLengthPacket) = 0;

	// May complete synchronously: when a packet arrived while the endpoint was not armed, the adapter delivers it from inside this
	// call, so OnTransferComplete() of the owner runs before StartOutTransfer() returns. The callers are interrupt context or a
	// critical section, so the callback sees the same exclusion as the interrupt. The nesting is bounded by the endpoint's
	// hardware buffers, and a function must not assume the call returned before its callback ran.
	virtual ResultStatus StartOutTransfer(uint8 address, std::span<uint8> buffer) = 0;

	virtual ResultStatus Stall(uint8 address) = 0;

	virtual ResultStatus ClearStall(uint8 address) = 0;

	virtual ResultStatus RemoteWakeup() = 0;

	// Answers to the control request that OnControlRequest() is handling. Valid only while OnControlRequest() runs; at any
	// other time they return notReady.
	//
	// Sends data in the IN data stage. The core sends min(data.size(), wLength) bytes and ends the stage correctly, including
	// the zero-length packet rule. The span must stay valid until the stage completes (the host has taken the last packet)
	// or the next SETUP, whichever comes first. Returns invalidParameter when wLength is 0 or the request is not an IN
	// request, and busy when the stage was already started for this request.
	virtual ResultStatus ControlSendData(std::span<const uint8> data) = 0;

	// Receives the OUT data stage into buffer. The core receives exactly wLength bytes, so buffer must hold at least wLength
	// bytes (else invalidParameter; a longer buffer is cut to wLength). When the bytes have arrived,
	// OnControlDataReceived() is called with them. The buffer must stay valid until OnControlDataReceived() returns or the
	// next SETUP, whichever comes first. Returns invalidParameter when wLength is 0 or the request is not an OUT request,
	// and busy when the stage was already started for this request.
	virtual ResultStatus ControlReceiveData(std::span<uint8> buffer) = 0;
};


// What the fluent builder produces for one function (USBDescriptors::Function()). The spans refer to static storage of the
// descriptor set, so they stay valid for the life of the device.
//   interfaces   the function's interface numbers
//   endpoints    the endpoint addresses in the order the class requested them (the order its constructor expects)
//   descriptors  the function's slice of the configuration descriptor, its interface association first when it has one
struct USBFunctionLayout {
	std::span<const uint8> interfaces;
	std::span<const uint8> endpoints;
	std::span<const uint8> descriptors;
};


// Base of one function: a class or vendor interface group inside the configuration. A composite configuration holds
// several functions. Each owns the interfaces and endpoints it was given, and the core routes to it the requests and the
// transfer completions for them.
//
// The interface numbers and endpoint addresses are the values the application wrote into its descriptors. The spans are
// kept, not copied, so the arrays must outlive the function (constexpr or static storage).
//
// Interrupt context: OnConfigured, OnDeconfigured, OnBusReset, OnSuspend, OnResume, OnControlRequest,
// OnControlDataReceived, OnTransferComplete, OnEndpointHaltChanged, OnSetInterface and GetInterface run in interrupt
// context, because the core calls them from the adapter's events. Keep them short; never block or allocate. Process()
// runs from the application's main loop or thread only.
class USBFunction {
protected:
	std::span<const uint8> interfaceNumbers;
	std::span<const uint8> endpointAddresses;


public:
	USBFunction(std::span<const uint8> interfaces, std::span<const uint8> endpoints)
		: interfaceNumbers(interfaces),
		  endpointAddresses(endpoints) {
	}


	// The layout of the fluent builder. The descriptor slice is not kept here: the function reads its numbers and addresses only.
	explicit USBFunction(const USBFunctionLayout& layout)
		: interfaceNumbers(layout.interfaces),
		  endpointAddresses(layout.endpoints) {
	}


	virtual ~USBFunction() = default;


	std::span<const uint8> GetInterfaces() const {
		return interfaceNumbers;
	}


	std::span<const uint8> GetEndpoints() const {
		return endpointAddresses;
	}


	bool OwnsInterface(uint8 interfaceNumber) const {
		for (uint8 number : interfaceNumbers) {
			if (number == interfaceNumber) {
				return true;
			}
		}

		return false;
	}


	bool OwnsEndpoint(uint8 address) const {
		for (uint8 endpoint : endpointAddresses) {
			if (endpoint == address) {
				return true;
			}
		}

		return false;
	}


	// Called when SET_CONFIGURATION selects the configuration. Opens the function's endpoints. Anything but ok makes the core
	// deconfigure and stall the request. Pure virtual: each function opens its own endpoints, so the core has no default to
	// fall back on.
	virtual ResultStatus OnConfigured(USBDeviceContext& context) = 0;


	// Called before the core closes the function's endpoints, on SET_CONFIGURATION(0) and on Stop(). The function drops its
	// pending work here.
	virtual void OnDeconfigured() {
	}


	// Called on bus reset after the core has closed every endpoint except endpoint 0. The function resets its state. The
	// core does not call OnDeconfigured on a bus reset.
	virtual void OnBusReset() {
	}


	virtual void OnSuspend() {
	}


	virtual void OnResume() {
	}


	// A class or vendor request, or a standard request the core does not handle, whose recipient is an interface or an
	// endpoint this function owns. Return:
	//   ok             handled. For a request with a data stage the function must have called ControlSendData (IN) or
	//                  ControlReceiveData (OUT) before returning. The core completes the request with its remaining stages.
	//                  A request without a data stage is completed with a status stage.
	//   anything else  the core stalls the request. notSupported is the usual value.
	// Default: notSupported.
	virtual ResultStatus OnControlRequest(const USBSetupPacket&, USBDeviceContext&) {
		return ResultStatus::notSupported;
	}


	// The OUT data stage that OnControlRequest() started with ControlReceiveData() has arrived. data is the received bytes.
	// Return ok to complete the request with a status stage, anything else to stall it. Default: notSupported.
	virtual ResultStatus OnControlDataReceived(const USBSetupPacket&, std::span<const uint8>, USBDeviceContext&) {
		return ResultStatus::notSupported;
	}


	// A transfer on one of the function's endpoints completed. byteCount is the bytes sent (IN) or stored (OUT). A function
	// that keeps a transfer going re-arms it here. A 0-byte OUT completion is no data, and the function re-arms it the same
	// way. Default: nothing.
	virtual void OnTransferComplete(uint8, uint32, USBDeviceContext&) {
	}


	// The halt of one of the function's non-control endpoints changed. halted is true after a successful Stall() and false
	// after ClearStall(), whether the host asked (SET/CLEAR_FEATURE ENDPOINT_HALT) or the function did through the context.
	// A stalled endpoint has no transfer in flight, because Stall() abandoned it: the function forgets its transfer, and
	// re-arms on clear. The call can run inside the Stall() or ClearStall() that caused it. Default: nothing.
	virtual void OnEndpointHaltChanged(uint8, bool, USBDeviceContext&) {
	}


	// SET_INTERFACE for one of the function's interfaces. Return ok when the alternate setting is applied, anything else to
	// stall. Default: only alternate 0 is accepted.
	virtual ResultStatus OnSetInterface(uint8, uint8 alternate, USBDeviceContext&) {
		if (alternate == 0) {
			return ResultStatus::ok;
		}

		return ResultStatus::notSupported;
	}


	// The alternate setting selected for an interface, for GET_INTERFACE. Default: 0.
	virtual uint8 GetInterface(uint8) const {
		return 0;
	}


	// Deferred work, run from the application's main loop or thread, never from an interrupt. Default: nothing.
	virtual void Process(USBDeviceContext&) {
	}
};
