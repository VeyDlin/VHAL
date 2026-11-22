#pragma once
#include "IAdapter.h"
#include <Adapter/Utilities/USBTypes.h>
#include <Utilities/VSTD/utility/Delegate.h>
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
// Lifecycle: SetHighestEndpointNumber() (optional), SetParameters(), assign the events, Connect(). Each bus reset opens
// endpoint 0 again. Disconnect() detaches from the bus and abandons transfers in flight without events.
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
//
// Isochronous endpoints (USB 2.0 section 5.6.4). One packet per frame, with no handshake and no retry. The model is the one the
// fsdev ports follow (Port/STM32/Common/USBFsdev.h). A port that moves packets another way says so in its own page.
//   - Opened with type Isochronous, a packet size of 1 to 1023 bytes and doubleBuffer false. The port always gives the endpoint
//     both buffers of its direction, so the flag is not used. A packet size the port cannot place in its memory is outOfMemory.
//   - StartInTransfer() and StartOutTransfer() arm one packet. An IN span holds at most maxPacketSize bytes (an empty span is a
//     zero-length packet, and sendZeroLengthPacket is ignored). An OUT buffer holds the packet. One transfer per direction: busy
//     while one is armed. A completion fires once the packet is done.
//   - Pace: the function arms the next packet in the completion callback of the packet before, so the packet is in place about a
//     frame before its token. Arming at the start of a frame races that frame's token (the host schedules periodic transfers early
//     in the frame), so a function does not arm there.
//   - IN: a packet armed from inside its own endpoint's completion callback (the streaming path) goes out at the host's token of the
//     next frame. A packet armed from anywhere else (thread context, the control request that selects the alternate setting, another
//     endpoint's callback) is pending: the next token of the endpoint sends a zero-length packet with no completion and writes the
//     packet, and the token after that sends it and completes. A packet armed while that endpoint's CTR_TX is already set, and not yet
//     served by the interrupt, goes out one token earlier: the interrupt writes it after the finished token, so it is sent at the next
//     token. The write never races a token, because it happens after a token has finished. onTransferComplete(address, size) fires when
//     the packet has left.
//     Every frame's token is answered. A frame with no packet armed is a zero-length IN packet: nothing completes, no event is
//     raised and the port counts nothing. The packet armed after a frame goes out at the following token, never at the one
//     before it, so a stale packet is never sent. The port keeps that true by writing a packet from outside the callback only at a token,
//     never while a token reads the buffer, so the write cannot race the host.
//   - The bound: the USB interrupt must be served within one frame of the token. A later service merges the completions of two tokens,
//     and the port cannot tell which buffer went out, so the completion and the arm that follows it go wrong.
//   - OUT: the packet of a frame is stored in the armed buffer and completes the transfer, with byteCount the bytes it carried:
//     0 for a zero-length packet, and any size up to the buffer. A packet that does not fit is dropped whole, with
//     onError(DataOverrun), and the completion is the bytes stored before it, which is 0 for a one-packet transfer. A frame with
//     no transfer armed is still stored by the hardware, and the port drops it: no event, and the packet is not kept for a later
//     transfer, so the host's stale packet is never delivered.
//   - No data toggle to reset and no retry. The port alternates the two buffers of the endpoint with each frame, whether or not
//     a packet was armed, so consecutive packets sit in different buffers. The function does not see the buffers. No PID is
//     assumed for an isochronous packet.
//   - No halt. Stall() returns notExist for a direction that is not open and invalidParameter for an open one, and ClearStall() returns ok and changes nothing, because an isochronous endpoint
//     is never halted. The core therefore refuses SET_FEATURE(ENDPOINT_HALT) on it with a stall, acknowledges
//     CLEAR_FEATURE(ENDPOINT_HALT), and GET_STATUS reports no halt (IsStalled() is false). Close, a bus reset and Disconnect()
//     work as they do for the other types: an armed packet is abandoned with no event.
//   - One number, one pair: the two directions of a number share one register, so an isochronous IN and an isochronous OUT on
//     the same number cannot both be open. The second one is notSupported (see OpenEndpoint()).
//   - Bus errors: fsdev reports no CRC per packet. A bus error raises its ERR flag, which the port reports as Error::Unknown,
//     and the packet of that frame does not complete the transfer, so the transfer waits for the next packet. A port that
//     can see a CRC error reports Error::Crc.
//   - Start of frame: onStartOfFrame, with startOfFrameEvents on, fires once per frame in interrupt context, before the
//     completions of that frame's interrupt. The core does not subscribe to it. It is for timekeeping, for example an application
//     that counts frames or paces a producer. The function arms from the completions, so it does not need the event for each packet.
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
		// Raises onStartOfFrame every 1 ms. Off by default because the interrupt costs time on every frame. The event runs in
		// interrupt context at the start of the frame, before the transfer completions of that frame's interrupt (see "Isochronous
		// endpoints" below for how an isochronous function is paced).
		bool startOfFrameEvents = false;
	};


	struct EndpointConfig {
		uint8 address = 0;
		USBEndpointType type = USBEndpointType::Bulk;
		// Full speed limits: 8, 16, 32 or 64 for bulk, up to 64 for interrupt, 1 to 1023 for isochronous. A port may refuse an isochronous size its
		// packet memory cannot hold (outOfMemory). Endpoint 0 is not opened here.
		uint16 maxPacketSize = 64;
		// Bulk only: two packet buffers, so the hardware keeps one busy while the other is handled. A port that cannot
		// provide it returns notSupported. Any other type with this set returns invalidParameter. An isochronous endpoint always
		// has its two buffers in the port, so it must leave this false (invalidParameter otherwise).
		bool doubleBuffer = false;
	};


	// Maximum packet size of endpoint 0. The descriptor builders check bMaxPacketSize0 against it.
	static constexpr uint16 ControlMaxPacketSize = 64;


	// Events. Assign them before Connect(). They run in interrupt context, so a handler must be short and must not block
	// or allocate. A delegate is a function pointer with a context, made with VSTD::Delegate::Bind() on a member function; a
	// lambda with captures does not fit. An empty delegate is skipped. To replace one while its interrupt may run, use the
	// SetOn... setter of that event.
	VSTD::Delegate<void(USBSpeed speed)> onBusReset;


	VSTD::Delegate<void(const USBSetupBytes& setup)> onSetup;


	// A transfer finished: byteCount is the bytes moved (sent for IN, stored for OUT, 0 for a zero-length packet). It fires
	// after the hardware has finished the transfer, so the transfer is no longer in flight inside the callback, and the
	// callback may arm the next transfer in the same direction. onError for the same transfer fires before it.
	VSTD::Delegate<void(uint8 address, uint32 byteCount)> onTransferComplete;


	VSTD::Delegate<void()> onSuspend;


	VSTD::Delegate<void()> onResume;


	VSTD::Delegate<void(uint16 frameNumber)> onStartOfFrame;


	VSTD::Delegate<void(Error error)> onError;


protected:
	Parameters parameters;


public:
	// Replaces onBusReset while its interrupt may run. See "Changing a callback while its interrupt may run" in the wiki.
	void SetOnBusReset(VSTD::Delegate<void(USBSpeed speed)> handler) {
		System::CriticalSection(true);
		onBusReset = handler;
		System::CriticalSection(false);
	}


	// Replaces onSetup while its interrupt may run. See "Changing a callback while its interrupt may run" in the wiki.
	void SetOnSetup(VSTD::Delegate<void(const USBSetupBytes& setup)> handler) {
		System::CriticalSection(true);
		onSetup = handler;
		System::CriticalSection(false);
	}


	// Replaces onTransferComplete while its interrupt may run. See "Changing a callback while its interrupt may run" in the wiki.
	void SetOnTransferComplete(VSTD::Delegate<void(uint8 address, uint32 byteCount)> handler) {
		System::CriticalSection(true);
		onTransferComplete = handler;
		System::CriticalSection(false);
	}


	// Replaces onSuspend while its interrupt may run. See "Changing a callback while its interrupt may run" in the wiki.
	void SetOnSuspend(VSTD::Delegate<void()> handler) {
		System::CriticalSection(true);
		onSuspend = handler;
		System::CriticalSection(false);
	}


	// Replaces onResume while its interrupt may run. See "Changing a callback while its interrupt may run" in the wiki.
	void SetOnResume(VSTD::Delegate<void()> handler) {
		System::CriticalSection(true);
		onResume = handler;
		System::CriticalSection(false);
	}


	// Replaces onStartOfFrame while its interrupt may run. See "Changing a callback while its interrupt may run" in the wiki.
	void SetOnStartOfFrame(VSTD::Delegate<void(uint16 frameNumber)> handler) {
		System::CriticalSection(true);
		onStartOfFrame = handler;
		System::CriticalSection(false);
	}


	// Replaces onError while its interrupt may run. See "Changing a callback while its interrupt may run" in the wiki.
	void SetOnError(VSTD::Delegate<void(Error error)> handler) {
		System::CriticalSection(true);
		onError = handler;
		System::CriticalSection(false);
	}


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


	// Optional. The device core calls it in Start(), before SetParameters(), with the highest endpoint number the configuration
	// descriptor names (0 to 15, endpoint 0 included). A port whose buffer table has an entry per endpoint number takes the value
	// at the next SetParameters(), so endpoint 0's buffers go above a table of the numbers in use. The port then refuses to open a
	// number above the value (invalidParameter). The default ignores the call and keeps the port's full table. Thread context.
	virtual ResultStatus SetHighestEndpointNumber(uint8) {
		return ResultStatus::ok;
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
	//   invalidParameter address is not a direction and number the configured endpointCount allows, or a port that sized its table
	//                    from SetHighestEndpointNumber() has no entry for the number, or the type, maximum
	//                    packet size or doubleBuffer do not fit (an isochronous endpoint with doubleBuffer set does not fit)
	//   busy             a transfer is in flight on the address
	//   notSupported     doubleBuffer is asked for and the port cannot give it
	//   outOfMemory      packet memory (fsdev) or FIFO space (dwc2) does not fit the request
	// Some ports share one register between the two directions of a number (fsdev). There the directions must have the same
	// type, and a double-buffered endpoint uses both directions of its number. So a doubleBuffer request beside an open other
	// direction is notSupported, and a single-buffered request beside a double-buffered other direction is invalidParameter:
	// the caller created that conflict. An isochronous endpoint has two buffers too, so an isochronous IN and OUT on one number
	// are notSupported as well. The refused call leaves the endpoints as they were.
	// The adapter allocates the memory and keeps it until CloseEndpoint() or CloseAllEndpoints().
	virtual ResultStatus OpenEndpoint(const EndpointConfig& config) = 0;


	// Closes an endpoint and releases its transfer buffer: the packet memory that OpenEndpoint() reserved for it. The port keeps
	// no use of that memory after the call. A transfer in flight is abandoned without a completion event. Returns
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
	// address, notExist for an endpoint that is not open, notReady while disconnected, and invalidParameter for an isochronous
	// span longer than maxPacketSize (see "Isochronous endpoints"). Callable from both contexts.
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
	// endpoint 0 halt (see the control stages above). Returns notExist for an endpoint that is not open, and invalidParameter for
	// an isochronous endpoint, which has no halt (see "Isochronous endpoints" above). Callable from both contexts; the port takes
	// no critical section.
	virtual ResultStatus Stall(uint8 address) = 0;


	// Clears the halt and resets the data toggle to DATA0 (USB 2.0 section 9.4.5). Returns notExist for an endpoint that is
	// not open, and ok for an isochronous endpoint, which has no halt and no toggle to reset, so the call changes nothing. Callable from both contexts; the port takes
	// no critical section, so a thread-context caller wraps the call in System::CriticalSection(), as for StartInTransfer().
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
		if (onBusReset) {
			onBusReset(speed);
		}
	}


	virtual inline void CallSetup(const USBSetupBytes& setup) {
		if (onSetup) {
			onSetup(setup);
		}
	}


	virtual inline void CallTransferComplete(uint8 address, uint32 byteCount) {
		if (onTransferComplete) {
			onTransferComplete(address, byteCount);
		}
	}


	virtual inline void CallSuspend() {
		if (onSuspend) {
			onSuspend();
		}
	}


	virtual inline void CallResume() {
		if (onResume) {
			onResume();
		}
	}


	virtual inline void CallStartOfFrame(uint16 frameNumber) {
		if (onStartOfFrame) {
			onStartOfFrame(frameNumber);
		}
	}


	virtual inline void CallError(Error error) {
		if (onError) {
			onError(error);
		}
	}
};
