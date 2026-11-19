#pragma once
#include "IAdapter.h"
#include <Adapter/Utilities/USBTypes.h>
#include <functional>
#include <span>

#define VHAL_USB_DEVICE_ADAPTER


// Device-side USB controller. It describes what the hardware does and nothing about USB requests: the Common device core
// (USBDevice) decides what the bus traffic means. Endpoint addresses are USB addresses (0x81 is endpoint 1 IN).
//
// Not a template, like SocketAdapter: the core takes it by reference and never sees the vendor handle. A port derives
// from it and keeps the handle (Port/STM32/Common/USBFsdev.h; USBDwc2.h later).
//
// Roles: the controller may serve the host role later (dwc2 is one controller for both). Nothing here assumes that the
// device role is the only one. A dual-role port owns the controller and sends its interrupt to the active part; settings
// of one role belong to that role's adapter, not to this one.
//
// Lifecycle: SetParameters(), assign the events, Connect(). Each bus reset opens endpoint 0 again. Disconnect() detaches
// from the bus and abandons transfers in flight without events.
//
// Endpoint 0 and control stages:
//   - The adapter opens control endpoint 0 (IN and OUT, ControlMaxPacketSize) itself on every bus reset and sets the
//     address to 0. It then calls onBusReset.
//   - SETUP packets are received on endpoint 0 by the adapter. The core never arms anything for them, and onSetup fires
//     for each one. A SETUP that arrives while a transfer is in flight on endpoint 0 abandons that transfer in both
//     directions without a completion event, and clears an endpoint 0 halt. The port clears the halt itself, before
//     onSetup, and does not rely on the hardware for it. A SETUP also discards a pending SET_ADDRESS (see SetAddress()).
//   - No endpoint 0 completion is reported after onSetup for a transfer started before that SETUP. The port reports the
//     completions the hardware finished before the SETUP first, and drops the rest.
//   - Data and status stages are ordinary transfers on 0x80 and 0x00. For a control request with wLength bytes the
//     core does this (n = the bytes it sends, at most wLength):
//       data IN:  StartInTransfer(0x80, data.first(n), n < wLength). On completion of 0x80: StartOutTransfer(0x00, empty).
//       data OUT: StartOutTransfer(0x00, buffer.first(wLength)). On completion of 0x00: StartInTransfer(0x80, empty, false).
//       no data:  StartInTransfer(0x80, empty, false).
//       The completion of the status transfer ends the request.
//     A rejected request is answered with Stall(0x80), which halts both endpoint 0 directions.
//   - SET_ADDRESS: the core calls SetAddress() when the request arrives. The address takes effect at the status stage
//     (see SetAddress()).
class USBDeviceAdapter : public IAdapter {
public:
	// Errors reported by the hardware. Errors on a healthy bus (for example a CRC error the host retries) are reported
	// only when the port sees them; the core counts them and does not act on most.
	enum class Error : uint8 {
		Crc,
		BitStuffing,
		// A received packet did not fit into the room left in the armed buffer. The packet is not stored (see
		// StartOutTransfer()).
		DataOverrun,
		Timeout,
		Framing,
		Unknown
	};


	struct Parameters {
		// Number of endpoint numbers in use, endpoint 0 included: numbers 0 to endpointCount - 1 may be opened.
		// SetParameters() returns invalidParameter for 0 or for more than the hardware has (8 on fsdev, 4 on F4 OTG_FS).
		// 4 is the default because every planned port has at least that many; an application that needs more sets it.
		uint8 endpointCount = 4;
		// Raises onStartOfFrame every 1 ms. Off by default because the interrupt costs time on every frame.
		bool startOfFrameEvents = false;
	};


	struct EndpointConfig {
		uint8 address = 0;
		USBEndpointType type = USBEndpointType::Bulk;
		// Full speed limits: 8, 16, 32 or 64 for bulk, up to 64 for interrupt. Endpoint 0 is not opened here.
		uint16 maxPacketSize = 64;
		// Bulk only: two packet buffers, so the hardware keeps one busy while the other is handled. A port that cannot
		// provide it returns notSupported. Any other type with this set returns invalidParameter.
		bool doubleBuffer = false;
	};


	// Maximum packet size of endpoint 0. The descriptor builders check bMaxPacketSize0 against it.
	static constexpr uint16 ControlMaxPacketSize = 64;


	// Events. Assign them before Connect(). They run in interrupt context, so a handler must be short and must not block
	// or allocate. A null function is skipped.
	std::function<void(USBSpeed speed)> onBusReset;


	std::function<void(const USBSetupBytes& setup)> onSetup;


	// A transfer finished: byteCount is the bytes moved (sent for IN, stored for OUT, 0 for a zero-length packet). It fires
	// after the hardware has finished the transfer, so the transfer is no longer in flight inside the callback, and the
	// callback may arm the next transfer in the same direction. onError for the same transfer fires before it.
	std::function<void(uint8 address, uint32 byteCount)> onTransferComplete;


	std::function<void()> onSuspend;


	std::function<void()> onResume;


	std::function<void(uint16 frameNumber)> onStartOfFrame;


	std::function<void(Error error)> onError;


protected:
	Parameters parameters;


public:
	// Stores val and runs Initialization(). Only while disconnected: returns busy while IsConnected(). Thread context.
	virtual ResultStatus SetParameters(Parameters val) {
		if (IsConnected()) {
			return ResultStatus::busy;
		}

		parameters = val;
		return Initialization();
	}


	const Parameters& GetParameters() const {
		return parameters;
	}


	// Sets the D+ pull-up, which makes the host see the device. Thread context, never from an interrupt. Returns notReady
	// before SetParameters() succeeded, and notSupported when the hardware has no software pull-up (the board then has a
	// fixed resistor and the device is attached from power-up). The core's Start treats notSupported as success.
	virtual ResultStatus Connect() = 0;


	// Removes the pull-up, which makes the host see a detach. Transfers in flight are abandoned without events. Endpoints
	// stay open: the core closes them. Thread context.
	virtual ResultStatus Disconnect() = 0;


	virtual bool IsConnected() const = 0;


	// Stores the address as pending and returns invalidParameter for an address above 127. Callable from interrupt context:
	// the core calls it from onSetup when SET_ADDRESS arrives, and then starts the status stage on 0x80 itself.
	//
	// The pending address takes effect at the status stage. The port applies it before it reports the next completion of
	// 0x80, which is that status (the core starts no other 0x80 transfer in between). A SETUP that arrives first discards
	// it. The core sets its own state at the same completion, so the two agree on every port.
	// Address 0 is the default address and every bus reset sets it.
	virtual ResultStatus SetAddress(uint8 address) = 0;


	// Opens an endpoint with the given config. An address that is already open is reconfigured, which is refused with busy
	// while a transfer is in flight on it. Endpoint 0 is refused with invalidParameter. Callable from thread context and
	// from interrupt context (the core calls it from SET_CONFIGURATION). Returns:
	//   ok               opened
	//   invalidParameter address is not a direction and number the configured endpointCount allows, or the type, maximum
	//                    packet size or doubleBuffer do not fit
	//   busy             a transfer is in flight on the address
	//   notSupported     doubleBuffer is asked for and the port cannot give it
	//   outOfMemory      packet memory (fsdev) or FIFO space (dwc2) does not fit the request
	// Some ports share one register between the two directions of a number (fsdev). There the directions must have the same
	// type, and a double-buffered endpoint uses both directions of its number. So a doubleBuffer request beside an open other
	// direction is notSupported, and a single-buffered request beside a double-buffered other direction is invalidParameter:
	// the caller created that conflict. The refused call leaves the endpoints as they were.
	// The adapter allocates the memory and keeps it until CloseEndpoint() or CloseAllEndpoints().
	virtual ResultStatus OpenEndpoint(const EndpointConfig& config) = 0;


	// Closes an endpoint and frees its memory. A transfer in flight is abandoned without a completion event. Returns
	// invalidParameter for endpoint 0 and notExist for an endpoint that is not open. Callable from both contexts.
	virtual ResultStatus CloseEndpoint(uint8 address) = 0;


	// Closes every endpoint except endpoint 0, as CloseEndpoint() does. Called on bus reset and on SET_CONFIGURATION(0).
	// Callable from both contexts; the port takes no critical section.
	virtual void CloseAllEndpoints() = 0;


	// Sends data on an IN endpoint (the address has bit 7 set). The adapter splits data into packets of the endpoint's
	// maximum packet size. When data is a whole number of packets and sendZeroLengthPacket is set, one zero-length packet
	// follows it, so the host sees the end of the transfer. Without the flag no such packet is sent. An empty span always
	// sends exactly one zero-length packet (the status stage of an IN control request), whatever the flag.
	// onTransferComplete(address, data.size()) fires when the host has taken the last packet. The span must stay valid until
	// then. One transfer per direction: returns busy while another is in flight. Returns invalidParameter for an OUT
	// address, notExist for an endpoint that is not open, notReady while disconnected. Callable from both contexts.
	//
	// The completion never runs inside this call. It follows the host's IN token, so it runs in interrupt context after the
	// call has returned: the fsdev port raises it once the hardware reports the last packet taken (IrqHandler()), and the sim
	// adapter when its host takes the last packet.
	// Unlike StartOutTransfer(), this call never completes synchronously. From thread context the caller wraps the call in
	// System::CriticalSection(), because the interrupt can complete the endpoint while the call is still arming it (fsdev).
	// The completion then runs when the section ends.
	virtual ResultStatus StartInTransfer(uint8 address, std::span<const uint8> data, bool sendZeroLengthPacket) = 0;


	// Arms reception into buffer on an OUT address (bit 7 clear), or on endpoint 0 for a data or status stage. The transfer
	// completes when buffer is full or with a short packet, and onTransferComplete(address, stored) fires with the number of
	// bytes stored. An empty buffer accepts one zero-length packet and completes with 0.
	//
	// A packet that does not fit the room left is not stored, not even partly: onError(DataOverrun) fires, then the transfer
	// completes with byteCount = the bytes stored before that packet. That count can be less than buffer.size(). The
	// overflowing packet is discarded whole.
	//
	// A zero-length packet that arrives after a transfer has completed with a full buffer completes the next armed transfer
	// with 0. A function treats a 0-byte completion as no data and re-arms.
	//
	// While no transfer is armed, an OUT endpoint NAKs and keeps the host's packet, and an IN endpoint NAKs. Neither drops
	// data. The buffer must stay valid until completion. The same return values as StartInTransfer, with invalidParameter
	// for an IN address. Callable from both contexts.
	//
	// The call may complete synchronously. A packet that arrived while the endpoint was not armed can be delivered from
	// inside the call: the port raises onTransferComplete before StartOutTransfer() returns, and the transfer is no longer
	// in flight by then (fsdev does this for a double-buffered bulk OUT endpoint, with the packet it held for this transfer).
	// A callback that arms the next transfer therefore runs inside this call, and may complete it in turn. That is safe
	// because every caller is in interrupt context or inside a critical section, so the callback sees the same exclusion as
	// the call that raised it. The callback may open a critical section of its own, because sections nest. The nesting is
	// bounded by the endpoint's hardware buffers: a packet is held only while no transfer is armed, and each buffer holds at
	// most one packet.
	// The call returns ok for a transfer it accepted, also when that transfer completed inside the call. A failure status means
	// nothing was armed and no callback follows, so a caller may roll back its own state on a failure only.
	virtual ResultStatus StartOutTransfer(uint8 address, std::span<uint8> buffer) = 0;


	// Halts an endpoint: the next IN or OUT token gets STALL. A transfer in flight is abandoned without an event.
	// Endpoint 0 has one halt for both directions, so Stall(0x00) and Stall(0x80) halt the control pipe. A SETUP clears the
	// endpoint 0 halt (see the control stages above). Returns notExist for an endpoint that is not open. Callable from both
	// contexts; the port takes no critical section.
	virtual ResultStatus Stall(uint8 address) = 0;


	// Clears the halt and resets the data toggle to DATA0 (USB 2.0 section 9.4.5). Returns notExist for an endpoint that is
	// not open. Callable from both contexts; the port takes no critical section, so a thread-context caller wraps the call in
	// System::CriticalSection(), as for StartInTransfer().
	// Exception to the IN contract: on a double-buffered IN endpoint (fsdev) a transfer the hardware had already finished can
	// complete inside this call. onTransferComplete then runs before ClearStall() returns, and the function may start its next
	// IN transfer there. The core calls the function's OnEndpointHaltChanged() only after this call returns, so a function must
	// not restart its IN while the halt is still set (VendorBulkFunctionBase keeps that rule).
	virtual ResultStatus ClearStall(uint8 address) = 0;


	// Callable from both contexts. Reads the halt only.
	virtual bool IsStalled(uint8 address) const = 0;


	// Starts resume signalling on the bus (K state, 1 to 15 ms, USB 2.0 section 7.1.7.7). It returns at once: the port ends
	// the signalling itself and then raises onResume, so the core restores its state from that event. Returns notReady when
	// the bus is not suspended. Callable from both contexts.
	// USB 2.0 section 7.1.7.7 keeps the device suspended for 5 ms before it drives resume. The core does not time that wait,
	// so the caller calls this at least 5 ms after the suspend. A call from OnSuspend() is too early.
	virtual ResultStatus RemoteWakeup() = 0;


	// Frame number of the last start-of-frame packet, 11 bits. Valid after the first SOF. Callable from both contexts.
	virtual uint16 GetFrameNumber() const = 0;


	// Called from the application's interrupt vector. Handles the controller interrupt and raises the events above.
	virtual void IrqHandler() = 0;


protected:
	// Checks the clock and applies parameters. Returns notReady when the USB peripheral clock is off. Calls
	// BeforeInitialization() and AfterInitialization() as the other adapters do.
	virtual ResultStatus Initialization() = 0;


	virtual inline void CallBusReset(USBSpeed speed) {
		if (onBusReset != nullptr) {
			onBusReset(speed);
		}
	}


	virtual inline void CallSetup(const USBSetupBytes& setup) {
		if (onSetup != nullptr) {
			onSetup(setup);
		}
	}


	virtual inline void CallTransferComplete(uint8 address, uint32 byteCount) {
		if (onTransferComplete != nullptr) {
			onTransferComplete(address, byteCount);
		}
	}


	virtual inline void CallSuspend() {
		if (onSuspend != nullptr) {
			onSuspend();
		}
	}


	virtual inline void CallResume() {
		if (onResume != nullptr) {
			onResume();
		}
	}


	virtual inline void CallStartOfFrame(uint16 frameNumber) {
		if (onStartOfFrame != nullptr) {
			onStartOfFrame(frameNumber);
		}
	}


	virtual inline void CallError(Error error) {
		if (onError != nullptr) {
			onError(error);
		}
	}
};
