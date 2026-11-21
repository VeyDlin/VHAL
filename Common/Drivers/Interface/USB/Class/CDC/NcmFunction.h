#pragma once
#include <Adapter/USBDeviceAdapter.h>
#include <Drivers/Interface/USB/Class/CDC/NcmNtb16.h>
#include <Drivers/Interface/USB/Class/CDC/USBCdcNcmBuilder.h>
#include <Drivers/Interface/USB/Class/CDC/USBCdcProtocol.h>
#include <Drivers/Interface/USB/Device/USBFunction.h>
#include <Drivers/Interface/USB/USBProtocol.h>
#include <System/System.h>
#include <Utilities/VSTD/utility/Delegate.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <span>


namespace USBNcmDetail {


// A fixed queue of Ethernet frames, with room for Depth frames of the largest size. The storage is inside the queue, so it needs no
// allocation. The queue is not thread safe: NcmFunction touches each queue from thread context only.
template<std::size_t Depth>
class FrameQueue {
	static_assert(Depth >= 1, "a frame queue holds at least one frame");


private:
	struct Entry {
		std::array<uint8, USBCdcMaxSegmentSize> data{};
		std::size_t length = 0;
	};


	std::array<Entry, Depth> entries{};
	std::size_t head = 0;
	std::size_t count = 0;


public:
	std::size_t Count() const {
		return count;
	}


	// Appends a copy of the frame. False, and nothing is stored, when the queue is full or the frame is longer than a frame.
	bool Push(std::span<const uint8> frame) {
		if (count == Depth || frame.size() > USBCdcMaxSegmentSize) {
			return false;
		}

		Entry& entry = entries[(head + count) % Depth];
		std::copy(frame.begin(), frame.end(), entry.data.begin());
		entry.length = frame.size();
		count++;
		return true;
	}


	// The frame at index, counted from the oldest. index must be below Count().
	std::span<const uint8> Peek(std::size_t index) const {
		const Entry& entry = entries[(head + index) % Depth];
		return std::span<const uint8>(entry.data.data(), entry.length);
	}


	// Removes the oldest n frames. n must not exceed Count().
	void Drop(std::size_t n) {
		head = (head + n) % Depth;
		count -= n;
	}


	void Clear() {
		head = 0;
		count = 0;
	}
};


}


// A CDC-NCM function: an Ethernet link. The host sees a network adapter, and each side moves whole Ethernet frames inside network
// transfer blocks (NTBs, NCM 1.0 section 3) over the bulk pair. The class builder USBCdcNcmBuilder lays out the interfaces; this class
// answers the NCM requests, runs the alternate settings of the data interface and moves the frames.
//
// Template parameters (the RAM the function reserves, see the wiki page):
//   InNtbSize   the largest NTB the device sends to the host, dwNtbInMaxSize. 2048 to 65535 bytes.
//   OutNtbSize  the largest NTB the host sends to the device, dwNtbOutMaxSize. 2048 to 65535 bytes.
//   TxFrames    frames queued by SendFrame() for the next NTB.
//   RxFrames    frames received and waiting for ReceiveFrame().
// The buffers take InNtbSize + OutNtbSize bytes and (TxFrames + RxFrames) frame slots, 1520 bytes each on the 32-bit target. The object
// holds more than the buffers: the counters, the delegates, the control reply and notification buffers, and the NTB builder's entry
// tables. The buffer sizes do not count those.
//
// Data path. Alternate 1 of the data interface opens the bulk endpoints and starts a session: the NTB sequence restarts at 0, the link
// comes up (CONNECTION_SPEED_CHANGE, then NETWORK_CONNECTION(1) on the notification endpoint) and the OUT endpoint is armed. Alternate 0
// closes the bulk endpoints, drops the queued frames and sends NETWORK_CONNECTION(0) when the host holds the link up, or a connection in flight
// says up. A bus reset and a deconfiguration do the same. Each session has a generation: a block that arrived in an earlier one is
// dropped, never parsed into the next. The NTB input size, the datagram limit and the packet filter go back to their defaults on
// alternate 0, a configuration, a deconfiguration and a bus reset. Alternate 1 keeps them, because the host sets them before it opens the path.
//   Receive: an OUT NTB is taken into the OUT buffer. Process() parses it and queues its frames. The endpoint is not armed again until
//            the buffer is parsed, so the host's next NTB is NAKed meanwhile. A block with any broken rule delivers nothing and is
//            counted by its rule. A frame that does not fit the queue is dropped and counted. A block of a closed session is dropped and
//            counted in ntbsDroppedStale, not in ntbsReceived. The frames it queued before the session ended are counted in
//            framesDroppedReceive, like the other received frames a session end clears.
//   Send:    SendFrame() queues a frame. Process() packs the queue into one IN NTB no larger than the host's NTB input size. The NTB is
//            sent as soon as the IN endpoint is idle. A block that is a multiple of 64 bytes and smaller than the host's input size is
//            followed by a zero-length packet. Frames queued while an NTB is in flight wait for the next one. The frames leave the queue
//            when the NTB is committed. A committed NTB that the endpoint has not taken is checked against the host's limits again before
//            it starts; when it no longer fits it is withdrawn and its frames are counted in framesDroppedSend. A session end withdraws an
//            NTB the same way, whether it waits or is in flight: the endpoint abandons an NTB in flight without a completion.
//
// Context. The USBFunction callbacks run in interrupt context and never allocate. The interrupt does not parse, pack or copy frames:
// it only starts transfers and sets the flags that Process() acts on. SendFrame(), ReceiveFrame(), NextFrameLength(), SetLinkUp() and
// Process() run in thread context, and so do the delegates of Process(). Only the state the interrupt reads is taken in short
// critical sections. The control replies and the data flags are kept in the object, because the adapter reads the reply buffers until
// their stage completes.
//
// The queues belong to thread context. A session change the interrupt makes is applied by the next thread call, which clears the queues
// first; PumpIn() does not read them past a pending change. The delegates of the data path run in interrupt context, so they must not call
// SendFrame(), ReceiveFrame(), NextFrameLength() or other queue API. The three calls refuse (busy, 0, 0) while System::IsInterrupt() is set.
//
// The MAC address. The Ethernet descriptor names the host's MAC address through a run-time string. MacStringIndex() gives its index for
// USBDevice::SetRuntimeString(); the descriptor is built with MakeUSBHexStringDescriptor() from the 6 MAC bytes. The device's own IP
// stack must use another address.
//
// Requests (on the communication interface; anything else is stalled):
//   GET_NTB_PARAMETERS (IN, 28 bytes), GET_NTB_FORMAT (IN, 2 bytes), SET_NTB_FORMAT (NTB-16 only: value 1 stalls),
//   GET_NTB_INPUT_SIZE (IN, 8 bytes), SET_NTB_INPUT_SIZE (OUT, 4 or 8 bytes; 2048 up to InNtbSize, otherwise stalled),
//   SET_ETHERNET_PACKET_FILTER (OUT, no data; stored and acknowledged, the filter is not used).
// Not advertised, so not implemented: encapsulated commands, net address, CRC mode and the maximum datagram size.
template<uint16 InNtbSize = 2048, uint16 OutNtbSize = 2048, std::size_t TxFrames = 4, std::size_t RxFrames = 4>
class NcmFunction : public USBFunction {
	static_assert(InNtbSize >= USBNcmMinNtbSize, "dwNtbInMaxSize is at least 2048 bytes");
	static_assert(OutNtbSize >= USBNcmMinNtbSize, "dwNtbOutMaxSize is at least 2048 bytes");
	static_assert(TxFrames >= 1 && RxFrames >= 1, "each frame queue holds at least one frame");


public:
	// Opened and stopped run in interrupt context, when the data path opens (alternate 1) or closes. Stopped runs only when the path was
	// open. They must be short, and must not call the queue API of this class (see the Context paragraph above).
	using DataPathDelegate = VSTD::Delegate<void()>;
	// A received NTB delivered at least one frame. It runs in Process(), in thread context.
	using FrameReceivedDelegate = VSTD::Delegate<void()>;


	// The counters of the function, all kept in thread context.
	struct Counters {
		uint32 framesReceived = 0;
		// Frames that did not fit the receive queue, and the received frames a session end cleared from it.
		uint32 framesDroppedReceive = 0;
		// Frames of the NTBs sent. Counted when the NTB is committed, and taken back when the NTB never reaches the host: withdrawn before the
		// endpoint takes it, or abandoned in flight by a session end.
		uint32 framesSent = 0;
		// Frames that were committed or queued for sending and never reached the host: an NTB withdrawn (the host's input size shrank below
		// it, or the session ended), an NTB abandoned in flight by a session end, and the queued frames a session end clears.
		uint32 framesDroppedSend = 0;
		uint32 ntbsReceived = 0;
		uint32 ntbsMalformed = 0;
		uint32 ntbsSent = 0;
		// Received NTBs dropped because their session had ended: waiting for the parse, or ended while it ran. They are not in ntbsReceived,
		// and no rule counts them, because they are not delivered.
		uint32 ntbsDroppedStale = 0;
	};


	// The line rate CONNECTION_SPEED_CHANGE reports, in each direction. A full-speed USB link carries 12 Mbit/s; the host uses the value
	// only to show a speed, not to pace the data.
	static constexpr uint32 LinkBitRate = 12000000;


private:
	static constexpr std::size_t NtbStatusCount = static_cast<std::size_t>(USBNcmNtb16Status::ndpChainTooLong) + 1;
	static constexpr uint8 AlternateIdle = 0;
	static constexpr uint8 AlternateActive = 1;


	DataPathDelegate startedHandler;
	DataPathDelegate stoppedHandler;
	FrameReceivedDelegate frameHandler;
	Counters counters;
	std::array<uint32, NtbStatusCount> malformed{};
	uint8 macStringIndex = 0;
	bool hasMacString = false;
	USBNcmNtbParameters parameters = MakeParameters();
	std::array<uint8, InNtbSize> inBuffer{};
	std::array<uint8, OutNtbSize> outBuffer{};
	USBNcmNtb16Builder<> builder;
	USBNcmDetail::FrameQueue<TxFrames> txQueue;
	USBNcmDetail::FrameQueue<RxFrames> rxQueue;
	// Session state. The interrupt writes it, thread context reads it inside a section or as a single flag.
	bool configured = false;
	bool dataPathActive = false;
	bool dataEndpointsOpen = false;
	uint8 selectedAlternate = AlternateIdle;
	bool linkUp = true;
	bool sessionResetPending = false;
	// The generation of the data path. The interrupt increments it on every close and reset, and a block records the generation it arrived in.
	uint32 sessionEpoch = 0;
	uint32 inputSize = InNtbSize;
	uint16 inputDatagrams = 0;
	uint32 packetFilter = 0;
	// The notification endpoint. One notification is in flight at a time, in notifyBuffer. hostUp is the link state the host last took, from
	// its last completed connection notification. notifyUp is the state of the connection in flight, valid while one is. The speed change
	// goes before the connection notification, as the host expects.
	uint8 notifyAddress = 0;
	bool notifyInFlight = false;
	bool notifyHalted = false;
	bool notifySpeed = false;
	bool notifyUp = false;
	bool speedPending = false;
	bool connectionPending = false;
	// The application's link change, set by SetLinkUp() while the host holds the other state. It is its own flag, not connectionPending,
	// so a change the host already holds, or one that a change back cancels, sends nothing.
	bool linkChangePending = false;
	bool hostUp = false;
	std::array<uint8, USBCdcConnectionSpeedChangeSize> notifyBuffer{};
	// The IN side: the NTB in inBuffer, of inNtbLength bytes made of inNtbFrames frames, with the sequence number inNtbSequence. It is ready
	// when it waits for the endpoint (inReady) and in flight when the endpoint has it (inFlight). The buffer is the thread's while neither
	// is set. withdrawnBlocks and withdrawnFrames count the NTBs that were withdrawn, waiting or in flight: the interrupt sets them on a session
	// end, the thread on a size the host lowered. The thread moves them into the counters.
	uint8 inAddress = 0;
	bool inFlight = false;
	bool inReady = false;
	bool inHalted = false;
	uint32 inNtbLength = 0;
	uint32 inNtbFrames = 0;
	uint16 inNtbSequence = 0;
	uint32 withdrawnBlocks = 0;
	uint32 withdrawnFrames = 0;
	// The OUT side: outArmed while the endpoint takes a block, outFull while outBuffer holds one of outLength bytes for Process(). The block
	// belongs to the session whose generation is outBlockEpoch.
	uint8 outAddress = 0;
	bool outArmed = false;
	bool outHalted = false;
	bool outFull = false;
	std::size_t outLength = 0;
	uint32 outBlockEpoch = 0;
	// The replies of the control requests. Each is the data stage's buffer until the stage completes.
	std::array<uint8, USBNcmNtbParametersSize> ntbParameterBytes{};
	std::array<uint8, 2> ntbFormatBytes{};
	std::array<uint8, 8> ntbInputSizeBytes{};


	// The parameters the function advertises: NTB-16 only, both sizes at the template's limits, the default divisors and no datagram limit.
	static USBNcmNtbParameters MakeParameters() {
		USBNcmNtbParameters values;
		values.formatsSupported = USBNcmNtbFormatNtb16;
		values.inMaxSize = InNtbSize;
		values.outMaxSize = OutNtbSize;
		return values;
	}


	// Reads a little-endian 32-bit value at offset. The caller checks that four bytes are there.
	static uint32 ReadUInt32(std::span<const uint8> bytes, std::size_t offset) {
		return static_cast<uint32>(bytes[offset])
			| (static_cast<uint32>(bytes[offset + 1]) << 8)
			| (static_cast<uint32>(bytes[offset + 2]) << 16)
			| (static_cast<uint32>(bytes[offset + 3]) << 24);
	}


public:
	// The function is not copied or moved: its builder and the span it keeps point into its own buffers.
	explicit NcmFunction(const USBFunctionLayout& layout)
		: USBFunction(layout),
		  builder(std::span<uint8>(inBuffer), parameters) {
		if (!layout.runtimeStrings.empty()) {
			macStringIndex = layout.runtimeStrings[0];
			hasMacString = true;
		}
	}


	NcmFunction(const NcmFunction&) = delete;


	NcmFunction& operator=(const NcmFunction&) = delete;


	NcmFunction(NcmFunction&&) = delete;


	NcmFunction& operator=(NcmFunction&&) = delete;


	// The run-time string index of the MAC address (iMACAddress). Pass it to USBDevice::SetRuntimeString() with the descriptor.
	uint8 MacStringIndex() const {
		return macStringIndex;
	}


	// The handlers are refused (busy) once the function is configured, because the callables run from the device's context.
	ResultStatus SetDataPathStartedHandler(DataPathDelegate value) {
		System::CriticalSection(true);
		if (configured) {
			System::CriticalSection(false);
			return ResultStatus::busy;
		}

		startedHandler = value;
		System::CriticalSection(false);
		return ResultStatus::ok;
	}


	ResultStatus SetDataPathStoppedHandler(DataPathDelegate value) {
		System::CriticalSection(true);
		if (configured) {
			System::CriticalSection(false);
			return ResultStatus::busy;
		}

		stoppedHandler = value;
		System::CriticalSection(false);
		return ResultStatus::ok;
	}


	ResultStatus SetFrameReceivedHandler(FrameReceivedDelegate value) {
		System::CriticalSection(true);
		if (configured) {
			System::CriticalSection(false);
			return ResultStatus::busy;
		}

		frameHandler = value;
		System::CriticalSection(false);
		return ResultStatus::ok;
	}


	// Queues one Ethernet frame (14 to 1514 bytes, without FCS) for the next IN NTB. Thread context. notReady while the data path is not
	// open, busy when the queue is full or the call comes from interrupt context, invalidParameter for a frame of another size. The frame
	// is copied, so the caller reuses its buffer.
	ResultStatus SendFrame(std::span<const uint8> frame) {
		// The queue is thread context only, so an interrupt caller is refused before the queue is touched.
		if (System::IsInterrupt()) {
			return ResultStatus::busy;
		}

		if (frame.size() < USBNcmMinDatagramSize || frame.size() > USBCdcMaxSegmentSize) {
			return ResultStatus::invalidParameter;
		}

		ApplySessionReset();
		System::CriticalSection(true);
		bool active = dataPathActive;
		System::CriticalSection(false);
		if (!active) {
			return ResultStatus::notReady;
		}

		if (!txQueue.Push(frame)) {
			return ResultStatus::busy;
		}

		return ResultStatus::ok;
	}


	// The length of the oldest received frame, or 0 when none waits. Check it before ReceiveFrame, which needs a span that holds the frame.
	// Thread context: from interrupt context it returns 0.
	uint16 NextFrameLength() {
		if (System::IsInterrupt()) {
			return 0;
		}

		ApplySessionReset();
		if (rxQueue.Count() == 0) {
			return 0;
		}

		return static_cast<uint16>(rxQueue.Peek(0).size());
	}


	// Copies the oldest received frame into frame and removes it from the queue. Returns the frame's length. Returns 0, and keeps the frame,
	// when none waits or frame is shorter than the frame: NextFrameLength() tells the two apart. From interrupt context it returns 0.
	uint16 ReceiveFrame(std::span<uint8> frame) {
		if (System::IsInterrupt()) {
			return 0;
		}

		ApplySessionReset();
		if (rxQueue.Count() == 0) {
			return 0;
		}

		std::span<const uint8> head = rxQueue.Peek(0);
		if (frame.size() < head.size()) {
			return 0;
		}

		std::copy(head.begin(), head.end(), frame.begin());
		uint16 length = static_cast<uint16>(head.size());
		rxQueue.Drop(1);
		return length;
	}


	// True while the host has the data path open (alternate 1 is selected).
	bool IsDataPathActive() const {
		return dataPathActive;
	}


	// The link state the application reports. While the data path is open, a change is sent as NETWORK_CONNECTION by the next Process().
	// The default is up.
	void SetLinkUp(bool up) {
		System::CriticalSection(true);
		linkUp = up;
		linkChangePending = dataPathActive && HostWillSeeUp() != up;
		System::CriticalSection(false);
	}


	bool IsLinkUp() const {
		return linkUp;
	}


	// The packet filter of the last SET_ETHERNET_PACKET_FILTER. The function does not filter with it.
	uint32 GetPacketFilter() const {
		return packetFilter;
	}


	Counters GetCounters() const {
		return counters;
	}


	// The number of received NTBs that broke the given rule. ok counts nothing.
	uint32 GetMalformedCount(USBNcmNtb16Status status) const {
		return malformed[static_cast<std::size_t>(status)];
	}


	ResultStatus OnConfigured(USBDeviceContext& context) override {
		configured = false;
		if (GetInterfaces().size() != 2 || GetEndpoints().size() != 3 || !hasMacString) {
			return ResultStatus::invalidParameter;
		}

		uint8 notifyIn = GetEndpoints()[0];
		if (!USBEndpoint::IsValid(notifyIn) || !USBEndpoint::IsIn(notifyIn) || USBEndpoint::Number(notifyIn) == 0) {
			return ResultStatus::invalidParameter;
		}

		USBDeviceAdapter::EndpointConfig config;
		config.address = notifyIn;
		config.type = USBEndpointType::Interrupt;
		config.maxPacketSize = USBCdcNcmBuilder::NotificationPacketSize;
		config.doubleBuffer = false;
		ResultStatus status = context.OpenEndpoint(config);
		if (status != ResultStatus::ok) {
			return status;
		}

		notifyAddress = notifyIn;
		inAddress = GetEndpoints()[1];
		outAddress = GetEndpoints()[2];
		ResetSession();
		configured = true;
		return ResultStatus::ok;
	}


	// SET_CONFIGURATION(0), a new configuration or Stop(). The core closes the endpoints after this call.
	void OnDeconfigured() override {
		ResetSession();
		configured = false;
	}


	// A bus reset closes every endpoint but endpoint 0, and the function starts again at alternate 0.
	void OnBusReset() override {
		ResetSession();
		configured = false;
	}


	ResultStatus OnControlRequest(const USBSetupPacket& setup, USBDeviceContext& context) override {
		if (setup.type != USBRequestType::Class || setup.index != CommunicationInterface()) {
			return ResultStatus::notSupported;
		}

		if (setup.request == USBCdcGetNtbParameters) {
			if (setup.direction != USBDirection::In) {
				return ResultStatus::notSupported;
			}

			ntbParameterBytes = USBNcmEncodeNtbParameters(parameters);
			return context.ControlSendData(ntbParameterBytes);
		}

		if (setup.request == USBCdcGetNtbFormat) {
			if (setup.direction != USBDirection::In) {
				return ResultStatus::notSupported;
			}

			USBNcmPut16(ntbFormatBytes, 0, USBCdcNtbFormat16);
			return context.ControlSendData(ntbFormatBytes);
		}

		if (setup.request == USBCdcSetNtbFormat) {
			if (setup.direction != USBDirection::Out || setup.length != 0) {
				return ResultStatus::notSupported;
			}

			return setup.value == USBCdcNtbFormat16 ? ResultStatus::ok : ResultStatus::notSupported;
		}

		if (setup.request == USBCdcGetNtbInputSize) {
			if (setup.direction != USBDirection::In) {
				return ResultStatus::notSupported;
			}

			USBNcmPut32(ntbInputSizeBytes, 0, inputSize);
			USBNcmPut16(ntbInputSizeBytes, 4, inputDatagrams);
			USBNcmPut16(ntbInputSizeBytes, 6, 0);
			return context.ControlSendData(ntbInputSizeBytes);
		}

		if (setup.request == USBCdcSetNtbInputSize) {
			if (setup.direction != USBDirection::Out || (setup.length != 4 && setup.length != 8)) {
				return ResultStatus::notSupported;
			}

			return context.ControlReceiveData(ntbInputSizeBytes);
		}

		if (setup.request == USBCdcSetEthernetPacketFilter) {
			if (setup.direction != USBDirection::Out || setup.length != 0) {
				return ResultStatus::notSupported;
			}

			packetFilter = setup.value;
			return ResultStatus::ok;
		}

		return ResultStatus::notSupported;
	}


	// The data of SET_NTB_INPUT_SIZE. The size must lie between 2048 bytes and InNtbSize; anything else is stalled, and the size stays.
	ResultStatus OnControlDataReceived(const USBSetupPacket& setup, std::span<const uint8> data, USBDeviceContext&) override {
		if (setup.type != USBRequestType::Class || setup.request != USBCdcSetNtbInputSize) {
			return ResultStatus::notSupported;
		}

		if (data.size() != 4 && data.size() != 8) {
			return ResultStatus::notSupported;
		}

		uint32 size = ReadUInt32(data, 0);
		if (size < USBNcmMinNtbSize || size > InNtbSize) {
			return ResultStatus::notSupported;
		}

		inputSize = size;
		inputDatagrams = data.size() == 8 ? USBNcmRead16(data, 4) : static_cast<uint16>(0);
		return ResultStatus::ok;
	}


	// Interrupt context. The IN endpoint sends the next NTB; the OUT endpoint's block is parsed in Process() before it is armed again.
	void OnTransferComplete(uint8 address, uint32 byteCount, USBDeviceContext& context) override {
		if (configured && address == notifyAddress) {
			// The host took the connection, so the state it holds is the one that was sent.
			if (notifyInFlight && !notifySpeed) {
				hostUp = notifyUp;
			}

			notifyInFlight = false;
			TryStartNotification(context);
			return;
		}

		if (dataEndpointsOpen && address == inAddress) {
			inFlight = false;
			TryStartReadyIn(context);
			return;
		}

		if (dataEndpointsOpen && address == outAddress) {
			outArmed = false;
			if (byteCount == 0) {
				ArmOut(context);
				return;
			}

			outLength = byteCount < OutNtbSize ? byteCount : OutNtbSize;
			outBlockEpoch = sessionEpoch;
			outFull = true;
		}
	}


	// Interrupt context. A halt abandons the transfer in flight. The IN NTB is still in its buffer, so it is ready to send again when the
	// halt clears. A lost notification is sent again too.
	void OnEndpointHaltChanged(uint8 address, bool halted, USBDeviceContext& context) override {
		if (configured && address == notifyAddress) {
			notifyHalted = halted;
			if (halted) {
				if (notifyInFlight) {
					notifyInFlight = false;
					if (notifySpeed) {
						speedPending = true;
					} else {
						connectionPending = true;
					}
				}

				return;
			}

			TryStartNotification(context);
			return;
		}

		if (dataEndpointsOpen && address == inAddress) {
			inHalted = halted;
			if (halted) {
				if (inFlight) {
					inFlight = false;
					inReady = true;
				}

				return;
			}

			TryStartReadyIn(context);
			return;
		}

		if (dataEndpointsOpen && address == outAddress) {
			outHalted = halted;
			if (halted) {
				outArmed = false;
				return;
			}

			ArmOut(context);
		}
	}


	// Alternate 1 opens the data path, alternate 0 closes it. Alternate 0 of the communication interface is the only setting it has.
	ResultStatus OnSetInterface(uint8 interfaceNumber, uint8 alternate, USBDeviceContext& context) override {
		if (GetInterfaces().size() != 2 || interfaceNumber != DataInterface()) {
			return alternate == AlternateIdle ? ResultStatus::ok : ResultStatus::notSupported;
		}

		if (alternate == AlternateIdle) {
			CloseDataPath(context);
			ResetParameters();
			TryStartNotification(context);
			return ResultStatus::ok;
		}

		if (alternate == AlternateActive) {
			return OpenDataPath(context);
		}

		return ResultStatus::notSupported;
	}


	uint8 GetInterface(uint8 interfaceNumber) const override {
		return GetInterfaces().size() == 2 && interfaceNumber == DataInterface() ? selectedAlternate : static_cast<uint8>(AlternateIdle);
	}


	// Thread context. Sends the queued frames, parses the received block, sends the pending notification and re-arms the OUT endpoint. A
	// refused arm is retried here, so an endpoint that was busy when it was armed gets its transfer as soon as the next call finds it free.
	void Process(USBDeviceContext& context) override {
		ApplySessionReset();
		ParseOut(context);
		PumpIn(context);
		FoldWithdrawn();
		System::CriticalSection(true);
		ArmOut(context);
		TryStartNotification(context);
		System::CriticalSection(false);
	}


private:
	uint8 CommunicationInterface() const {
		return GetInterfaces().empty() ? 0 : GetInterfaces()[0];
	}


	uint8 DataInterface() const {
		return GetInterfaces().size() < 2 ? 0 : GetInterfaces()[1];
	}


	// Thread context. Applies a session change the interrupt asked for: the frames of the old session go, and the NTB sequence restarts.
	// The interrupt only sets the flag, because it must not touch the queues the application reads. The frames that go are counted: they
	// were accepted, and they never reach the host or the application. The flag is cleared before the queues, so a change the interrupt
	// makes meanwhile is applied by the next call.
	void ApplySessionReset() {
		System::CriticalSection(true);
		bool pending = sessionResetPending;
		sessionResetPending = false;
		System::CriticalSection(false);
		if (pending) {
			counters.framesDroppedSend += static_cast<uint32>(txQueue.Count());
			counters.framesDroppedReceive += static_cast<uint32>(rxQueue.Count());
			rxQueue.Clear();
			txQueue.Clear();
			builder.SetSequence(0);
		}
	}


	// Interrupt context. Opens the bulk pair and starts the session. The link comes up with the speed change, then the connection.
	ResultStatus OpenDataPath(USBDeviceContext& context) {
		if (dataPathActive) {
			CloseDataPath(context);
		}

		USBDeviceAdapter::EndpointConfig inConfig;
		inConfig.address = inAddress;
		inConfig.type = USBEndpointType::Bulk;
		inConfig.maxPacketSize = USBCdcNcmBuilder::BulkPacketSize;
		inConfig.doubleBuffer = false;
		ResultStatus status = context.OpenEndpoint(inConfig);
		if (status != ResultStatus::ok) {
			return status;
		}

		USBDeviceAdapter::EndpointConfig outConfig;
		outConfig.address = outAddress;
		outConfig.type = USBEndpointType::Bulk;
		outConfig.maxPacketSize = USBCdcNcmBuilder::BulkPacketSize;
		outConfig.doubleBuffer = false;
		status = context.OpenEndpoint(outConfig);
		if (status != ResultStatus::ok) {
			context.CloseEndpoint(inAddress);
			return status;
		}

		dataEndpointsOpen = true;
		dataPathActive = true;
		selectedAlternate = AlternateActive;
		sessionResetPending = true;
		speedPending = true;
		connectionPending = true;
		ArmOut(context);
		if (startedHandler) {
			startedHandler();
		}

		TryStartNotification(context);
		return ResultStatus::ok;
	}


	// Interrupt context. Closes the bulk pair, if open. The notifications the session queued are dropped, except a disconnect: the host hears
	// of the close only when it holds the link up, or a connection in flight is about to give it that state. A connection the host never
	// took is not announced as down, and a connection halted before it was taken leaves the host with nothing to undo.
	void CloseDataPath(USBDeviceContext& context) {
		if (dataEndpointsOpen) {
			context.CloseEndpoint(inAddress);
			context.CloseEndpoint(outAddress);
		}

		ResetDataPath();
		connectionPending = HostWillSeeUp();
	}


	// Interrupt context. The state of the data path is dropped: the transfers in flight are gone, and the frames go at the next thread call.
	// A block that waits for the IN endpoint, or is in flight, is withdrawn, and its frames are counted as dropped by the thread. An NTB in
	// flight is abandoned by the endpoint with no completion, so the host never takes it. The generation moves on, so a block that arrived
	// in this session is dropped. The endpoints are not touched here; the caller closes them or the core does.
	void ResetDataPath() {
		bool wasActive = dataPathActive;
		if (inReady || inFlight) {
			withdrawnBlocks++;
			withdrawnFrames += inNtbFrames;
		}

		dataPathActive = false;
		dataEndpointsOpen = false;
		selectedAlternate = AlternateIdle;
		inFlight = false;
		inReady = false;
		inHalted = false;
		outArmed = false;
		outHalted = false;
		speedPending = false;
		// A link change asked for the path that is now closed. Its state goes to the host through connectionPending, if at all.
		linkChangePending = false;
		sessionResetPending = true;
		sessionEpoch++;
		if (wasActive && stoppedHandler) {
			stoppedHandler();
		}
	}


	// Interrupt context. A session ends on deconfiguration or a bus reset: the data path closes, the notification state is forgotten, so
	// the next configuration starts with the host seeing no link, and the host's NTB and packet filter settings go back to the defaults.
	void ResetSession() {
		ResetDataPath();
		ResetParameters();
		notifyInFlight = false;
		notifyHalted = false;
		connectionPending = false;
		hostUp = false;
	}


	// Interrupt context. The host's NTB input size, its datagram limit and the packet filter go back to their defaults. Alternate 0, a
	// configuration, a deconfiguration and a bus reset do this. Alternate 1 does not: the host sets these before it opens the path.
	void ResetParameters() {
		inputSize = InNtbSize;
		inputDatagrams = 0;
		packetFilter = 0;
	}


	// Interrupt context, or inside a section. True when a block of the given size and frame count fits the host's current input size and
	// datagram limit. The limits are read by the interrupt too, so the caller checks them inside a section.
	bool BlockFits(uint32 length, uint32 frames) const {
		return length <= inputSize && (inputDatagrams == 0 || frames <= inputDatagrams);
	}


	// Interrupt context, or inside a section. The link state the host holds once the notification in flight is taken: the state of a
	// connection in flight, or else the state the host last took. A speed change in flight says nothing about the link.
	bool HostWillSeeUp() const {
		if (notifyInFlight && !notifySpeed) {
			return notifyUp;
		}

		return hostUp;
	}


	// Interrupt context, or inside a section. Starts the next notification when none is in flight: the speed change first, then the link
	// state, when connectionPending or linkChangePending asks for it. A refused start keeps both requests pending for the next call.
	void TryStartNotification(USBDeviceContext& context) {
		if (!configured || notifyInFlight || notifyHalted) {
			return;
		}

		if (speedPending) {
			notifyBuffer = MakeUSBCdcConnectionSpeedChangeNotification(CommunicationInterface(), LinkBitRate, LinkBitRate);
			speedPending = false;
			notifyInFlight = true;
			notifySpeed = true;
			if (context.StartInTransfer(notifyAddress, notifyBuffer, false) != ResultStatus::ok) {
				notifyInFlight = false;
				speedPending = true;
			}

			return;
		}

		if (connectionPending || linkChangePending) {
			bool up = dataPathActive && linkUp;
			std::array<uint8, USBCdcNetworkConnectionSize> packet = MakeUSBCdcNetworkConnectionNotification(CommunicationInterface(), up);
			std::copy(packet.begin(), packet.end(), notifyBuffer.begin());
			bool wasConnectionPending = connectionPending;
			bool wasLinkChangePending = linkChangePending;
			connectionPending = false;
			linkChangePending = false;
			notifyInFlight = true;
			notifySpeed = false;
			notifyUp = up;
			std::span<const uint8> sent = std::span<const uint8>(notifyBuffer).first(USBCdcNetworkConnectionSize);
			if (context.StartInTransfer(notifyAddress, sent, false) != ResultStatus::ok) {
				notifyInFlight = false;
				connectionPending = wasConnectionPending;
				linkChangePending = wasLinkChangePending;
				return;
			}
		}
	}


	// Interrupt context, or inside a section. Arms the OUT endpoint on the OUT buffer when the path is open and the buffer is free.
	// outArmed is set before the call: a transfer that completes inside the call clears it again.
	void ArmOut(USBDeviceContext& context) {
		if (!dataPathActive || outArmed || outFull || outHalted) {
			return;
		}

		outArmed = true;
		if (context.StartOutTransfer(outAddress, outBuffer) != ResultStatus::ok) {
			outArmed = false;
		}
	}


	// Interrupt context, or inside a section. Starts the IN endpoint on the NTB that waits in inBuffer, when the endpoint is free and the block
	// still fits the host's limits. A block that does not fit stays ready: PumpIn() withdraws it, since the thread owns the counters. The
	// zero-length packet rule: a block that is a whole number of packets and smaller than the host's NTB input size ends with one.
	bool TryStartReadyIn(USBDeviceContext& context) {
		if (!inReady || inFlight || inHalted || !dataPathActive || !BlockFits(inNtbLength, inNtbFrames)) {
			return false;
		}

		bool zeroLength = inNtbLength % USBCdcNcmBuilder::BulkPacketSize == 0 && inNtbLength < inputSize;
		inFlight = true;
		inReady = false;
		std::span<const uint8> block = std::span<const uint8>(inBuffer).first(inNtbLength);
		if (context.StartInTransfer(inAddress, block, zeroLength) != ResultStatus::ok) {
			inFlight = false;
			inReady = true;
			return false;
		}

		return true;
	}


	// Thread context. Packs the queued frames into one NTB in inBuffer under the given limits, and returns the NTB's length, or 0 when no frame
	// fits. The frames stay queued: the caller removes them when it commits the NTB. frames gets the number of frames in it. The builder
	// advances the sequence number, so a caller that discards the NTB rewinds it.
	uint32 BuildInNtb(uint32 size, uint16 datagrams, uint32& frames) {
		USBNcmNtbLimits limits;
		limits.maxNtbSize = size;
		limits.maxDatagrams = datagrams;
		limits.maxDatagramSize = USBCdcMaxSegmentSize;
		frames = 0;
		if (!builder.Reset(limits)) {
			return 0;
		}

		while (frames < txQueue.Count() && builder.TryAdd(txQueue.Peek(frames))) {
			frames++;
		}

		if (frames == 0) {
			return 0;
		}

		return builder.Finish();
	}


	// Thread context. Moves the withdrawn NTBs into the counters. Their frames were counted as sent when they were committed, and they never
	// reached the host, so the sent counts go back and the frames count as dropped. The interrupt only sets the counts, so they are read here.
	void FoldWithdrawn() {
		System::CriticalSection(true);
		uint32 blocks = withdrawnBlocks;
		uint32 frames = withdrawnFrames;
		withdrawnBlocks = 0;
		withdrawnFrames = 0;
		System::CriticalSection(false);
		counters.ntbsSent -= blocks;
		counters.framesSent -= frames;
		counters.framesDroppedSend += frames;
	}


	// Thread context. Starts the IN side: a ready NTB first, withdrawn when it no longer fits the host's limits, then a new NTB from the queue
	// when the endpoint is free. The NTB is built outside the sections, because it copies the frames. The build takes the input limits and
	// the session generation in one section, and it is not taken while a session change is pending. The build reads the queue outside the
	// sections, so a change the interrupt makes meanwhile is caught at the commit: the NTB is committed only when the generation is
	// unchanged, no change is pending and it fits the limits the host holds at the commit. Otherwise it is discarded, its sequence number is
	// rewound, and the frames stay queued for the next call.
	void PumpIn(USBDeviceContext& context) {
		System::CriticalSection(true);
		if (dataPathActive && !inHalted && !inFlight && inReady) {
			if (BlockFits(inNtbLength, inNtbFrames)) {
				TryStartReadyIn(context);
			} else {
				inReady = false;
				withdrawnBlocks++;
				withdrawnFrames += inNtbFrames;
				builder.SetSequence(inNtbSequence);
			}
		}

		uint32 snapshotSize = inputSize;
		uint16 snapshotDatagrams = inputDatagrams;
		uint32 snapshotEpoch = sessionEpoch;
		// A session change the interrupt has not applied yet leaves the queue alone: the frames in it belong to the session that ended.
		bool wanted = dataPathActive && !sessionResetPending && !inHalted && !inFlight && !inReady && txQueue.Count() > 0;
		System::CriticalSection(false);
		if (!wanted) {
			return;
		}

		uint16 sequenceBefore = builder.Sequence();
		uint32 frames = 0;
		uint32 length = BuildInNtb(snapshotSize, snapshotDatagrams, frames);
		if (length == 0) {
			return;
		}

		System::CriticalSection(true);
		bool current = dataPathActive && !sessionResetPending && snapshotEpoch == sessionEpoch && !inHalted && !inFlight && !inReady
			&& BlockFits(length, frames);
		if (current) {
			txQueue.Drop(frames);
			counters.framesSent += frames;
			counters.ntbsSent++;
			inNtbLength = length;
			inNtbFrames = frames;
			inNtbSequence = sequenceBefore;
			inReady = true;
			TryStartReadyIn(context);
		}

		System::CriticalSection(false);
		if (!current) {
			builder.SetSequence(sequenceBefore);
		}
	}


	// Thread context. Parses the block in the OUT buffer when one is full, then arms the OUT endpoint again. A block of the current session is
	// checked and delivered. A block of a session that has ended is dropped and counted in ntbsDroppedStale, whether it was waiting for the
	// parse or the session ended while it was parsed. It is not counted in ntbsReceived. The frames it queued before the session ended are
	// counted in framesDroppedReceive, because ApplySessionReset clears the receive queue. The buffer is the thread's until outFull is
	// cleared, so the interrupt cannot arm the endpoint over it meanwhile.
	void ParseOut(USBDeviceContext& context) {
		System::CriticalSection(true);
		bool full = outFull;
		uint32 blockEpoch = outBlockEpoch;
		bool current = dataPathActive && blockEpoch == sessionEpoch;
		System::CriticalSection(false);
		if (!full) {
			return;
		}

		USBNcmNtb16Status status = USBNcmNtb16Status::ok;
		uint32 queued = 0;
		uint32 dropped = 0;
		if (current) {
			status = DeliverNtb(std::span<const uint8>(outBuffer).first(outLength), queued, dropped);
		}

		bool notify = false;
		System::CriticalSection(true);
		bool stillCurrent = dataPathActive && blockEpoch == sessionEpoch;
		outFull = false;
		ArmOut(context);
		if (current && stillCurrent) {
			notify = RecordBlock(status, queued, dropped);
		}

		System::CriticalSection(false);
		if (!current || !stillCurrent) {
			// The block belongs to a session that has ended. Its frames go with the session: the reset counts the ones queued before the end.
			counters.ntbsDroppedStale++;
			ApplySessionReset();
			return;
		}

		if (notify && frameHandler) {
			frameHandler();
		}
	}


	// Thread context. Checks the block and queues its frames, and returns the rule that failed, or ok. queued and dropped count the frames
	// that were queued and the ones that did not fit. A block with a broken rule delivers nothing.
	USBNcmNtb16Status DeliverNtb(std::span<const uint8> block, uint32& queued, uint32& dropped) {
		USBNcmNtbLimits limits;
		limits.maxNtbSize = OutNtbSize;
		limits.maxDatagrams = 0;
		limits.maxDatagramSize = USBCdcMaxSegmentSize;
		auto push = [this, &queued, &dropped](std::span<const uint8> frame) {
			if (rxQueue.Push(frame)) {
				queued++;
			} else {
				dropped++;
			}
		};
		return USBNcmNtb16ForEachDatagram(block, limits, push);
	}


	// Thread context, inside a section. Counts a parsed block and its frames. Returns true when the application has frames to read.
	bool RecordBlock(USBNcmNtb16Status status, uint32 queued, uint32 dropped) {
		counters.ntbsReceived++;
		if (status != USBNcmNtb16Status::ok) {
			counters.ntbsMalformed++;
			malformed[static_cast<std::size_t>(status)]++;
			return false;
		}

		counters.framesReceived += queued;
		counters.framesDroppedReceive += dropped;
		return queued > 0;
	}
};
