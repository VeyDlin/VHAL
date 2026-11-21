#pragma once
#include <Adapter/USBDeviceAdapter.h>
#include <Drivers/Interface/USB/Class/CDC/USBCdcAcmBuilder.h>
#include <Drivers/Interface/USB/Class/CDC/USBCdcProtocol.h>
#include <Drivers/Interface/USB/Class/Common/BufferedBulkChannel.h>
#include <Drivers/Interface/USB/Class/Common/BulkEndpointEngine.h>
#include <Drivers/Interface/USB/Device/USBFunction.h>
#include <Drivers/Interface/USB/USBProtocol.h>
#include <System/System.h>
#include <Utilities/VSTD/utility/Delegate.h>
#include <array>
#include <span>


// A CDC-ACM function: a serial port. Its data interface is the bulk engine in buffered mode, so it has the same API as the vendor
// bulk function: Write() and Read() through two ISR-safe rings, and Process() from the application's loop to send. Its
// communication interface carries the class requests of the abstract control model (PSTN 1.2): SET_LINE_CODING and
// GET_LINE_CODING, SET_CONTROL_LINE_STATE and SEND_BREAK. A class request to any other interface, or any other class request, is
// stalled.
//
// The endpoints are the ones USBCdcAcmBuilder lays out: notification IN (interrupt), bulk IN and bulk OUT, in that order. The
// interfaces are the communication interface and the data interface, in that order.
//
// Notifications: SetSerialState() sets the UART state bitmap, and Process() sends it as a SERIAL_STATE notification on the
// interrupt IN endpoint. Only one notification is in flight at a time, so the bytes of an unsent one are not overwritten: a change
// made while one is in flight is sent when that one completes, with the newest state. The state is sent once after configuration,
// so the host learns it, and again after a clear of the endpoint's halt.
//
// The line coding, the control lines and the break are handed to the application through delegates. The delegates run in interrupt
// context, when the host's request arrives, so they must be short. They are set before the device starts, as the stream bench does.
//
// TxBufferSize and RxBufferSize are the ring sizes in bytes (see BufferedBulkChannel). PacketsPerTransfer is the length of each
// bulk IN transfer buffer in packets. Each bulk OUT transfer is one packet, so a host write of any length reaches Read() whole.
template<uint16 TxBufferSize, uint16 RxBufferSize, uint8 PacketsPerTransfer = 8>
class CdcAcmFunction : public USBFunction, private BulkEndpointEngine {
public:
	// SET_LINE_CODING: the coding the host asked for, after it is stored.
	using LineCodingDelegate = VSTD::Delegate<void(const USBCdcLineCoding& coding)>;
	// SET_CONTROL_LINE_STATE: DTR and RTS as the host sets them. DTR is the "terminal open" signal.
	using ControlLineDelegate = VSTD::Delegate<void(bool dtr, bool rts)>;
	// SEND_BREAK: the duration in milliseconds, 0 to end a break, 0xFFFF for a break that lasts until the next SEND_BREAK(0).
	using BreakDelegate = VSTD::Delegate<void(uint16 durationMs)>;


private:
	BufferedBulkChannel<TxBufferSize, RxBufferSize, PacketsPerTransfer> channel;
	LineCodingDelegate lineCodingHandler;
	ControlLineDelegate controlLineHandler;
	BreakDelegate breakHandler;
	USBCdcLineCoding lineCoding;
	// The encoded coding of GET_LINE_CODING, and the block SET_LINE_CODING receives. Each is the data stage's buffer until it completes.
	std::array<uint8, USBCdcLineCodingSize> lineCodingBytes{};
	std::array<uint8, USBCdcLineCodingSize> lineCodingReceived{};
	// The notification being sent. The endpoint reads it until the transfer completes, so it is written only while none is in flight.
	std::array<uint8, USBCdcSerialStateSize> notificationBuffer{};
	uint16 serialState = 0;
	uint16 controlLines = 0;
	uint8 notifyAddress = 0;
	bool notifyConfigured = false;
	bool notifyInFlight = false;
	bool notifyHalted = false;
	// Set when the state differs from the one last sent, or when one must be sent (after configuration and a clear of the halt).
	bool notifyPending = false;


public:
	CdcAcmFunction(std::span<const uint8> interfaces, std::span<const uint8> endpoints)
		: USBFunction(interfaces, endpoints) {
		SetStages(channel.GetInStages(), channel.GetOutStage());
	}


	// From the fluent builder's layout (USBDescriptors::Function()).
	explicit CdcAcmFunction(const USBFunctionLayout& layout)
		: CdcAcmFunction(layout.interfaces, layout.endpoints) {
	}


	// The engine keeps spans into this object, so a copy or a move would leave them pointing into the source.
	CdcAcmFunction(const CdcAcmFunction&) = delete;


	CdcAcmFunction& operator=(const CdcAcmFunction&) = delete;


	CdcAcmFunction(CdcAcmFunction&&) = delete;


	CdcAcmFunction& operator=(CdcAcmFunction&&) = delete;


	// True while the device is configured. The bootloader's transport reads it.
	using BulkEndpointEngine::IsConfigured;


	// The session counter, for a caller that must notice a reset between two of its calls.
	using BulkEndpointEngine::GetSessionGeneration;


	// Queues bytes for the host. Returns how many were queued: fewer than data.size() when the TX ring fills, and 0 while the
	// function is not configured.
	uint16 Write(std::span<const uint8> data) {
		if (!IsConfigured()) {
			return 0;
		}

		return channel.Write(data);
	}


	// Copies received bytes into data and returns how many were copied. Returns 0 while the function is not configured.
	uint16 Read(std::span<uint8> data) {
		if (!IsConfigured()) {
			return 0;
		}

		return channel.Read(data);
	}


	// Received bytes waiting for Read().
	uint16 GetReadableCount() const {
		if (!IsConfigured()) {
			return 0;
		}

		return channel.GetReadableCount();
	}


	// Bytes Write() can queue now.
	uint16 GetWritableCount() const {
		if (!IsConfigured()) {
			return 0;
		}

		return channel.GetWritableCount();
	}


	// Sets the UART state bitmap (PSTN 1.2 table 31: RX carrier, TX carrier, break, ring, framing, parity, overrun). Process() sends
	// it to the host as a notification. A state equal to the last one is not sent again.
	void SetSerialState(uint16 bits) {
		System::CriticalSection(true);
		if (bits != serialState) {
			serialState = bits;
			notifyPending = true;
		}

		System::CriticalSection(false);
	}


	// True while the host has DTR set (the "terminal open" signal of the control lines). It is cleared when the device is
	// deconfigured or reset.
	bool IsTerminalOpen() const {
		return (controlLines & USBCdcControlDtr) != 0;
	}


	// The line coding the host last set, or the default 115200 8N1 before it sets one.
	USBCdcLineCoding GetLineCoding() const {
		System::CriticalSection(true);
		USBCdcLineCoding coding = lineCoding;
		System::CriticalSection(false);
		return coding;
	}


	// The handlers are refused (busy) once the device is configured, because the callables run from the device's context.
	ResultStatus SetLineCodingHandler(LineCodingDelegate value) {
		System::CriticalSection(true);
		if (IsConfigured()) {
			System::CriticalSection(false);
			return ResultStatus::busy;
		}

		lineCodingHandler = value;
		System::CriticalSection(false);
		return ResultStatus::ok;
	}


	ResultStatus SetControlLineStateHandler(ControlLineDelegate value) {
		System::CriticalSection(true);
		if (IsConfigured()) {
			System::CriticalSection(false);
			return ResultStatus::busy;
		}

		controlLineHandler = value;
		System::CriticalSection(false);
		return ResultStatus::ok;
	}


	ResultStatus SetBreakHandler(BreakDelegate value) {
		System::CriticalSection(true);
		if (IsConfigured()) {
			System::CriticalSection(false);
			return ResultStatus::busy;
		}

		breakHandler = value;
		System::CriticalSection(false);
		return ResultStatus::ok;
	}


	ResultStatus OnConfigured(USBDeviceContext& context) override {
		notifyConfigured = false;
		if (GetInterfaces().size() != 2 || GetEndpoints().size() != 3) {
			return ResultStatus::invalidParameter;
		}

		uint8 notifyIn = GetEndpoints()[0];
		if (!USBEndpoint::IsValid(notifyIn) || !USBEndpoint::IsIn(notifyIn) || USBEndpoint::Number(notifyIn) == 0) {
			return ResultStatus::invalidParameter;
		}

		USBDeviceAdapter::EndpointConfig config;
		config.address = notifyIn;
		config.type = USBEndpointType::Interrupt;
		config.maxPacketSize = USBCdcAcmBuilder::NotificationPacketSize;
		config.doubleBuffer = false;
		ResultStatus status = context.OpenEndpoint(config);
		if (status != ResultStatus::ok) {
			return status;
		}

		status = BeginSession(context, GetEndpoints()[1], GetEndpoints()[2]);
		if (status != ResultStatus::ok) {
			context.CloseEndpoint(notifyIn);
			return status;
		}

		notifyAddress = notifyIn;
		notifyInFlight = false;
		notifyHalted = false;
		notifyPending = true;
		notifyConfigured = true;
		return ResultStatus::ok;
	}


	// SET_CONFIGURATION(0), a new configuration (the old one is dropped first) or Stop(): the session ends, so DTR drops with it.
	void OnDeconfigured() override {
		EndSession();
		notifyConfigured = false;
		notifyInFlight = false;
		notifyHalted = false;
		SetControlLines(0);
	}


	// A bus reset: the device returns to its default line coding and control lines. The state of the port is kept, and it is sent again
	// after the next configuration.
	void OnBusReset() override {
		ResetSession();
		notifyConfigured = false;
		notifyInFlight = false;
		notifyHalted = false;
		notifyPending = false;
		lineCoding = USBCdcLineCoding();
		SetControlLines(0);
	}


	ResultStatus OnControlRequest(const USBSetupPacket& setup, USBDeviceContext& context) override {
		// The communication interface carries the requests. The data interface has none, so its requests are stalled.
		if (setup.type != USBRequestType::Class || setup.index != CommunicationInterface()) {
			return ResultStatus::notSupported;
		}

		if (setup.request == USBCdcSetLineCoding) {
			if (setup.direction != USBDirection::Out || setup.length != USBCdcLineCodingSize) {
				return ResultStatus::notSupported;
			}

			return context.ControlReceiveData(lineCodingReceived);
		}

		if (setup.request == USBCdcGetLineCoding) {
			if (setup.direction != USBDirection::In) {
				return ResultStatus::notSupported;
			}

			lineCodingBytes = USBCdcEncodeLineCoding(lineCoding);
			return context.ControlSendData(lineCodingBytes);
		}

		if (setup.request == USBCdcSetControlLineState) {
			if (setup.direction != USBDirection::Out || setup.length != 0) {
				return ResultStatus::notSupported;
			}

			SetControlLines(setup.value);
			return ResultStatus::ok;
		}

		if (setup.request == USBCdcSendBreak) {
			if (setup.direction != USBDirection::Out || setup.length != 0) {
				return ResultStatus::notSupported;
			}

			if (breakHandler) {
				breakHandler(setup.value);
			}

			return ResultStatus::ok;
		}

		return ResultStatus::notSupported;
	}


	ResultStatus OnControlDataReceived(const USBSetupPacket& setup, std::span<const uint8> data, USBDeviceContext&) override {
		if (setup.request != USBCdcSetLineCoding || data.size() != USBCdcLineCodingSize) {
			return ResultStatus::notSupported;
		}

		USBCdcLineCoding coding = USBCdcDecodeLineCoding(data);
		// A coding the port cannot run is stalled, so the host sees the refusal and the stored coding stays as it was.
		if (!IsValidLineCoding(coding)) {
			return ResultStatus::notSupported;
		}

		lineCoding = coding;
		if (lineCodingHandler) {
			lineCodingHandler(lineCoding);
		}

		return ResultStatus::ok;
	}


	void OnTransferComplete(uint8 address, uint32 byteCount, USBDeviceContext& context) override {
		if (notifyConfigured && address == notifyAddress) {
			notifyInFlight = false;
			TryStartNotification(context);
			return;
		}

		HandleTransferComplete(address, byteCount, context);
	}


	// A halt of the notification endpoint abandons the notification in flight without an event, so it is forgotten. The state is sent
	// again when the halt clears: the host needs the current state, not the one that was lost.
	void OnEndpointHaltChanged(uint8 address, bool halted, USBDeviceContext& context) override {
		if (notifyConfigured && address == notifyAddress) {
			notifyHalted = halted;
			if (halted) {
				notifyInFlight = false;
				return;
			}

			notifyPending = true;
			TryStartNotification(context);
			return;
		}

		HandleEndpointHalt(address, halted, context);
	}


	void Process(USBDeviceContext& context) override {
		Pump(context);
		// Only a pending notification needs the section. The flag is read without it: a stale read only postpones the start to the next
		// call, and TryStartNotification() checks the flags again inside the section.
		if (notifyPending) {
			System::CriticalSection(true);
			TryStartNotification(context);
			System::CriticalSection(false);
		}
	}


protected:
	// The engine's hooks, served by the ring channel.
	size_t ProduceIn(std::span<uint8> packets) override {
		return channel.ProduceIn(packets);
	}


	void CommitIn(size_t count) override {
		channel.CommitIn(count);
	}


	void RollbackIn() override {
		channel.RollbackIn();
	}


	bool HasMoreIn() const override {
		return channel.HasQueuedIn();
	}


	// The host of a ring channel reads until a short packet, so an idle run ends with a zero-length packet.
	bool EndsIdleRunsWithZeroLengthPacket() const override {
		return true;
	}


	bool CanAcceptOut() const override {
		return channel.CanAcceptOut();
	}


	void StoreOut(std::span<const uint8> data) override {
		channel.StoreOut(data);
	}


	void ResetData() override {
		channel.ResetData();
	}


private:
	uint8 CommunicationInterface() const {
		return GetInterfaces().empty() ? 0 : GetInterfaces()[0];
	}


	// Sets the control lines (DTR and RTS). The application's handler runs only when a line changes.
	void SetControlLines(uint16 value) {
		uint16 next = static_cast<uint16>(value & (USBCdcControlDtr | USBCdcControlRts));
		if (next == controlLines) {
			return;
		}

		controlLines = next;
		if (controlLineHandler) {
			controlLineHandler((next & USBCdcControlDtr) != 0, (next & USBCdcControlRts) != 0);
		}
	}


	// The codings PSTN 1.2 section 6.3.11 allows: a non-zero rate, stop-bit code up to 2, parity code up to 4, and 5, 6, 7, 8 or 16
	// data bits. A host that sends anything else gets a stall, not a port that runs with a coding it cannot use.
	static bool IsValidLineCoding(const USBCdcLineCoding& coding) {
		if (coding.dteRate == 0 || coding.stopBits > 2 || coding.parity > 4) {
			return false;
		}

		return coding.dataBits == 5 || coding.dataBits == 6 || coding.dataBits == 7 || coding.dataBits == 8 || coding.dataBits == 16;
	}


	// Starts the notification when one is pending and the endpoint is free. A failed start keeps it pending, so the next call retries.
	// Called from the interrupt, and from Process() inside a section.
	void TryStartNotification(USBDeviceContext& context) {
		if (!notifyConfigured || notifyInFlight || notifyHalted || !notifyPending) {
			return;
		}

		notificationBuffer = MakeUSBCdcSerialStateNotification(CommunicationInterface(), serialState);
		notifyPending = false;
		notifyInFlight = true;
		if (context.StartInTransfer(notifyAddress, notificationBuffer, false) != ResultStatus::ok) {
			notifyInFlight = false;
			notifyPending = true;
		}
	}
};
