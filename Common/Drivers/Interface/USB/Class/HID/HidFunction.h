#pragma once
#include <Drivers/Interface/USB/Class/HID/USBHidReportBuilder.h>
#include <Drivers/Interface/USB/Device/USBFunction.h>
#include <Drivers/Interface/USB/USBProtocol.h>
#include <System/System.h>
#include <Utilities/VSTD/utility/Delegate.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <span>


// A HID interface: the class requests of HID 1.11 chapter 7, the input reports the application sends, and the output and feature
// reports it receives. The report descriptor is the one the application built with USBHidReportDescriptor, and the HID descriptor
// and the endpoints come from the function's layout. Both were written by the same USBHidBuilder.
//
// Input reports: SendReport() queues a report from thread context. Process() and the completion of the IN transfer send the queue,
// one report at a time, in order. A full queue returns busy. A report sent while the device is suspended requests remote wakeup
// when the host enabled it. Boot protocol sends the first 8 (keyboard) or 3 (mouse) bytes of a report.
//
// Idle: SET_IDLE sets a period in 4 ms units for one report ID (0 for all). A non-zero period makes Process() queue a copy of the
// last input report of that ID each period, while the queue holds no report of that ID. The copy is the steady report that
// SendReport() keeps, so a relative device repeats no motion. A zero period sends only what the application sends.
//
// Output reports arrive from the OUT endpoint (when the interface has one) or from SET_REPORT(Output). Both call OutputDelegate in
// interrupt context. Feature reports go through FeatureGetDelegate and FeatureSetDelegate, also in interrupt context.
//
// Context: the function keeps no pointer to the device. SendReport() and the set-delegate calls are thread context and take a short
// critical section. The callbacks of USBFunction run in interrupt context and touch no lock of their own, so a call from the thread
// that changes state the interrupt reads is made inside a section. Sections nest, so a transfer start made inside the function's
// own section is safe, and the sections stay short.
template<std::size_t QueueDepth>
class HidFunction : public USBFunction {
	static_assert(QueueDepth >= 1, "the report queue holds at least one report");


public:
	// The largest report, one full-speed packet. Every buffer of the function is this size.
	static constexpr std::size_t MaxReportLength = USBHidMaxReportLength;
	// The distinct report IDs (of all types together) the function keeps state for: the idle period and the last input and output.
	static constexpr std::size_t MaxReportIds = 4;


	// Output report, with its report ID byte when the descriptor uses IDs. Called in interrupt context. The span is valid only during
	// the call.
	using OutputDelegate = VSTD::Delegate<void(std::span<const uint8> report)>;


	// Feature report requested by GET_REPORT(Feature). Fills report, which is exactly the feature report's length, and returns true,
	// or returns false to stall the request. Called in interrupt context.
	using FeatureGetDelegate = VSTD::Delegate<bool(uint8 reportId, std::span<uint8> report)>;


	// Feature report sent by SET_REPORT(Feature). Returns false to stall the request. Called in interrupt context.
	using FeatureSetDelegate = VSTD::Delegate<bool(std::span<const uint8> report)>;


private:
	static constexpr uint8 InterfaceDescriptorType = 0x04;
	static constexpr uint8 EndpointDescriptorType = 0x05;
	static constexpr uint8 HidDescriptorType = 0x21;
	static constexpr uint8 ReportDescriptorType = 0x22;
	static constexpr std::size_t DescriptorLength = 9;
	static constexpr uint8 ClassGetReport = 0x01;
	static constexpr uint8 ClassGetIdle = 0x02;
	static constexpr uint8 ClassGetProtocol = 0x03;
	static constexpr uint8 ClassSetReport = 0x09;
	static constexpr uint8 ClassSetIdle = 0x0A;
	static constexpr uint8 ClassSetProtocol = 0x0B;
	static constexpr uint8 BootSubClass = 0x01;
	static constexpr uint8 BootKeyboardProtocol = 0x01;
	static constexpr uint8 BootMouseProtocol = 0x02;
	static constexpr uint8 ProtocolBoot = 0x00;
	static constexpr uint8 ProtocolReport = 0x01;
	static constexpr std::size_t BootKeyboardLength = 8;
	static constexpr std::size_t BootMouseLength = 3;
	static constexpr uint64 IdleUnitMs = 4;


	struct QueuedReport {
		std::array<uint8, MaxReportLength> data{};
		std::size_t length = 0;
		uint8 reportId = 0;
	};


	struct ReportSlot {
		uint8 reportId = 0;
		bool hasInput = false;
		uint8 idle = 0;
		uint64 nextIdleTick = 0;
		std::array<uint8, MaxReportLength> input{};
		std::array<uint8, MaxReportLength> output{};
	};


	USBHidReportInfo report;
	std::span<const uint8> interfaceDescriptor;
	std::span<const uint8> hidDescriptor;
	bool layoutValid = false;
	uint8 inAddress = 0;
	bool hasOut = false;
	uint8 outAddress = 0;
	std::size_t inPacketSize = 0;
	std::size_t outPacketSize = 0;
	bool bootSubClass = false;
	std::size_t bootLength = 0;
	std::array<ReportSlot, MaxReportIds> slots{};
	std::size_t slotCount = 0;
	std::array<QueuedReport, QueueDepth> queue{};
	std::size_t queueHead = 0;
	std::size_t queueCount = 0;
	bool configured = false;
	bool inFlight = false;
	bool inHalted = false;
	bool outArmed = false;
	bool outHalted = false;
	bool suspended = false;
	bool wakeRequested = false;
	uint8 protocol = ProtocolReport;
	std::array<uint8, MaxReportLength> outBuffer{};
	std::array<uint8, MaxReportLength> setBuffer{};
	std::array<uint8, MaxReportLength> replyBuffer{};
	OutputDelegate outputDelegate;
	FeatureGetDelegate featureGetDelegate;
	FeatureSetDelegate featureSetDelegate;


public:
	// The layout is the function's slice of the configuration (USBDescriptors::Function()) and the report is the same
	// USBHidReportDescriptor::Info the HID class builder was given. OnConfigured() refuses a function whose layout does not match
	// the report: the HID descriptor must carry the report's length, and the layout must have the interface, the HID descriptor and
	// one IN and at most one OUT endpoint.
	HidFunction(const USBFunctionLayout& layout, const USBHidReportInfo& reportInfo)
		: USBFunction(layout),
		  report(reportInfo) {
		interfaceDescriptor = FindDescriptor(layout.descriptors, InterfaceDescriptorType);
		hidDescriptor = FindDescriptor(layout.descriptors, HidDescriptorType);
		layoutValid = interfaceDescriptor.size() == DescriptorLength && hidDescriptor.size() == DescriptorLength;
		if (layoutValid) {
			std::size_t reportLength = static_cast<std::size_t>(hidDescriptor[7] | (hidDescriptor[8] << 8));
			layoutValid = reportLength == report.descriptor.size();
		}

		if (layoutValid) {
			bootSubClass = interfaceDescriptor[6] == BootSubClass;
			uint8 interfaceProtocol = interfaceDescriptor[7];
			if (interfaceProtocol == BootKeyboardProtocol) {
				bootLength = BootKeyboardLength;
			} else if (interfaceProtocol == BootMouseProtocol) {
				bootLength = BootMouseLength;
			}
		}

		layoutValid = layoutValid && ReadEndpoints(layout.descriptors);
		layoutValid = layoutValid && BuildSlots();
	}


	// Output reports of the interface. Set before the device starts: busy once the function is configured.
	ResultStatus SetOutputDelegate(OutputDelegate value) {
		System::CriticalSection(true);
		if (configured) {
			System::CriticalSection(false);
			return ResultStatus::busy;
		}

		outputDelegate = value;
		System::CriticalSection(false);
		return ResultStatus::ok;
	}


	// Feature reports of GET_REPORT. Set before the device starts: busy once the function is configured.
	ResultStatus SetFeatureGetDelegate(FeatureGetDelegate value) {
		System::CriticalSection(true);
		if (configured) {
			System::CriticalSection(false);
			return ResultStatus::busy;
		}

		featureGetDelegate = value;
		System::CriticalSection(false);
		return ResultStatus::ok;
	}


	// Feature reports of SET_REPORT. Set before the device starts: busy once the function is configured.
	ResultStatus SetFeatureSetDelegate(FeatureSetDelegate value) {
		System::CriticalSection(true);
		if (configured) {
			System::CriticalSection(false);
			return ResultStatus::busy;
		}

		featureSetDelegate = value;
		System::CriticalSection(false);
		return ResultStatus::ok;
	}


	// Queues an input report from thread context. The span is the whole report: it begins with the report ID byte when the descriptor
	// uses IDs, and its length is the input report's length (invalidParameter otherwise). notReady before the device is configured,
	// busy when the queue is full. The report is copied, so the caller may reuse its buffer at once.
	ResultStatus SendReport(std::span<const uint8> data) {
		return SendReport(data, data);
	}


	// Queues an input report and keeps steady as the report that GET_REPORT and the idle repeat answer with. A relative device (a mouse)
	// sends its motion once; steady is the same report with the motion taken out, so a repeat does not move the pointer again. steady
	// has the length of data and the same report ID.
	ResultStatus SendReport(std::span<const uint8> data, std::span<const uint8> steady) {
		uint8 reportId = 0;
		if (report.UsesReportIds()) {
			if (data.empty()) {
				return ResultStatus::invalidParameter;
			}

			reportId = data[0];
			if (reportId == 0) {
				return ResultStatus::invalidParameter;
			}
		}

		std::size_t length = report.InputLength(reportId);
		if (length == 0 || data.size() != length) {
			return ResultStatus::invalidParameter;
		}

		if (steady.size() != length || (report.UsesReportIds() && steady[0] != reportId)) {
			return ResultStatus::invalidParameter;
		}

		System::CriticalSection(true);
		if (!configured) {
			System::CriticalSection(false);
			return ResultStatus::notReady;
		}

		if (queueCount == QueueDepth) {
			System::CriticalSection(false);
			return ResultStatus::busy;
		}

		ReportSlot* slot = FindSlot(reportId);
		if (slot != nullptr) {
			std::copy_n(steady.begin(), length, slot->input.begin());
			slot->hasInput = true;
		}

		Enqueue(reportId, data);
		System::CriticalSection(false);
		return ResultStatus::ok;
	}


	// Free places in the report queue. A report in flight holds its place until its transfer completes.
	std::size_t FreeSlots() {
		System::CriticalSection(true);
		std::size_t free = QueueDepth - queueCount;
		System::CriticalSection(false);
		return free;
	}


	// The report descriptor and the report lengths of the interface.
	const USBHidReportInfo& Report() const {
		return report;
	}


	// Sends the queue and runs the idle repeats, from the application's main loop or thread.
	void Process(USBDeviceContext& context) override {
		RepeatIdleReports();
		StartFromThread(context);
		RequestWakeup(context);
	}


	ResultStatus OnConfigured(USBDeviceContext& context) override {
		if (!layoutValid) {
			return ResultStatus::invalidParameter;
		}

		USBDeviceAdapter::EndpointConfig inConfig;
		inConfig.address = inAddress;
		inConfig.type = USBEndpointType::Interrupt;
		inConfig.maxPacketSize = static_cast<uint16>(inPacketSize);
		inConfig.doubleBuffer = false;
		ResultStatus status = context.OpenEndpoint(inConfig);
		if (status != ResultStatus::ok) {
			return status;
		}

		if (hasOut) {
			USBDeviceAdapter::EndpointConfig outConfig;
			outConfig.address = outAddress;
			outConfig.type = USBEndpointType::Interrupt;
			outConfig.maxPacketSize = static_cast<uint16>(outPacketSize);
			outConfig.doubleBuffer = false;
			status = context.OpenEndpoint(outConfig);
			if (status != ResultStatus::ok) {
				return status;
			}
		}

		ClearQueue();
		inFlight = false;
		inHalted = false;
		outArmed = false;
		outHalted = false;
		protocol = ProtocolReport;
		wakeRequested = false;
		ResetIdle();
		configured = true;
		status = ArmOut(context);
		if (status != ResultStatus::ok) {
			configured = false;
		}

		return status;
	}


	// Called from Stop() inside the caller's critical section, and from SET_CONFIGURATION(0). No section is entered here.
	void OnDeconfigured() override {
		configured = false;
		ClearQueue();
		inFlight = false;
		outArmed = false;
		ResetIdle();
	}


	void OnBusReset() override {
		configured = false;
		ClearQueue();
		inFlight = false;
		inHalted = false;
		outArmed = false;
		outHalted = false;
		protocol = ProtocolReport;
		suspended = false;
		wakeRequested = false;
		ResetIdle();
	}


	void OnSuspend() override {
		suspended = true;
		wakeRequested = false;
	}


	void OnResume() override {
		suspended = false;
		wakeRequested = false;
	}


	ResultStatus OnControlRequest(const USBSetupPacket& setup, USBDeviceContext& context) override {
		if (setup.recipient != USBRecipient::Interface || !layoutValid) {
			return ResultStatus::notSupported;
		}

		if (setup.type == USBRequestType::Standard) {
			return StandardRequest(setup, context);
		}

		if (setup.type != USBRequestType::Class) {
			return ResultStatus::notSupported;
		}

		if (setup.request == ClassGetReport) {
			return GetReport(setup, context);
		}

		if (setup.request == ClassSetReport) {
			return SetReport(setup, context);
		}

		if (setup.request == ClassGetIdle) {
			return GetIdle(setup, context);
		}

		if (setup.request == ClassSetIdle) {
			return SetIdle(setup);
		}

		if (setup.request == ClassGetProtocol) {
			return GetProtocol(setup, context);
		}

		if (setup.request == ClassSetProtocol) {
			return SetProtocol(setup);
		}

		return ResultStatus::notSupported;
	}


	// The data of SET_REPORT. Output and feature reports go to their delegates; an input report is kept as the last one.
	ResultStatus OnControlDataReceived(const USBSetupPacket& setup, std::span<const uint8> data, USBDeviceContext&) override {
		USBHidReportType type = USBHidReportType::Input;
		if (!ReportTypeOf(setup.value, type)) {
			return ResultStatus::notSupported;
		}

		uint8 reportId = static_cast<uint8>(setup.value & 0xFF);
		if (type == USBHidReportType::Feature) {
			if (!featureSetDelegate) {
				return ResultStatus::notSupported;
			}

			return featureSetDelegate(data) ? ResultStatus::ok : ResultStatus::notSupported;
		}

		ReportSlot* slot = FindSlot(reportId);
		if (slot == nullptr) {
			return ResultStatus::notSupported;
		}

		if (type == USBHidReportType::Output) {
			std::copy_n(data.begin(), data.size(), slot->output.begin());
			if (outputDelegate) {
				outputDelegate(data);
			}

			return ResultStatus::ok;
		}

		std::copy_n(data.begin(), data.size(), slot->input.begin());
		slot->hasInput = true;
		return ResultStatus::ok;
	}


	// Output report from the OUT endpoint. A packet of another length than the output report is dropped. The endpoint is armed again.
	void OnTransferComplete(uint8 address, uint32 byteCount, USBDeviceContext& context) override {
		if (!configured) {
			return;
		}

		if (address == inAddress) {
			if (!inFlight) {
				return;
			}

			inFlight = false;
			queueHead = (queueHead + 1) % QueueDepth;
			queueCount--;
			if (CanStart()) {
				inFlight = true;
				SubmitHead(context);
			}

			return;
		}

		if (hasOut && address == outAddress) {
			outArmed = false;
			std::size_t received = byteCount < MaxReportLength ? byteCount : MaxReportLength;
			if (received > 0) {
				DeliverOutput(std::span<const uint8>(outBuffer.data(), received));
			}

			ArmOut(context);
		}
	}


	// A stall abandons the transfer in flight, so the head is sent again after the clear. A clear starts the head at once.
	void OnEndpointHaltChanged(uint8 address, bool halted, USBDeviceContext& context) override {
		if (address == inAddress) {
			inHalted = halted;
			if (halted) {
				inFlight = false;
				return;
			}

			if (CanStart()) {
				inFlight = true;
				SubmitHead(context);
			}

			return;
		}

		if (hasOut && address == outAddress) {
			outHalted = halted;
			if (halted) {
				outArmed = false;
			} else {
				ArmOut(context);
			}
		}
	}


private:
	// The first descriptor of the given type in a run of length-prefixed descriptors, or an empty span.
	static std::span<const uint8> FindDescriptor(std::span<const uint8> bytes, uint8 type) {
		std::size_t offset = 0;
		while (offset + 2 <= bytes.size()) {
			std::size_t length = bytes[offset];
			if (length < 2 || offset + length > bytes.size()) {
				return std::span<const uint8>();
			}

			if (bytes[offset + 1] == type) {
				return bytes.subspan(offset, length);
			}

			offset += length;
		}

		return std::span<const uint8>();
	}


	// The max packet size of the endpoint descriptor for address, or 0 when the layout has none.
	static std::size_t FindPacketSize(std::span<const uint8> bytes, uint8 address) {
		std::span<const uint8> endpoint = FindEndpoint(bytes, address);
		if (endpoint.size() != 7) {
			return 0;
		}

		return static_cast<std::size_t>(endpoint[4] | (endpoint[5] << 8));
	}


	static std::span<const uint8> FindEndpoint(std::span<const uint8> bytes, uint8 address) {
		std::size_t offset = 0;
		while (offset + 2 <= bytes.size()) {
			std::size_t length = bytes[offset];
			if (length < 2 || offset + length > bytes.size()) {
				return std::span<const uint8>();
			}

			if (bytes[offset + 1] == EndpointDescriptorType && length == 7 && bytes[offset + 2] == address) {
				return bytes.subspan(offset, length);
			}

			offset += length;
		}

		return std::span<const uint8>();
	}


	// Reads the endpoint addresses of the layout: one IN endpoint, and at most one OUT endpoint.
	bool ReadEndpoints(std::span<const uint8> descriptors) {
		bool inFound = false;
		for (uint8 address : GetEndpoints()) {
			if (USBEndpoint::IsIn(address)) {
				if (inFound) {
					return false;
				}

				inFound = true;
				inAddress = address;
				inPacketSize = FindPacketSize(descriptors, address);
			} else {
				if (hasOut) {
					return false;
				}

				hasOut = true;
				outAddress = address;
				outPacketSize = FindPacketSize(descriptors, address);
			}
		}

		// The buffers of the function are one report long, so a packet of more than that cannot be served.
		if (!inFound || inPacketSize == 0 || inPacketSize < report.MaxLength(USBHidReportType::Input) || inPacketSize > MaxReportLength) {
			return false;
		}

		return !hasOut || (outPacketSize > 0 && outPacketSize >= report.MaxLength(USBHidReportType::Output) && outPacketSize <= MaxReportLength);
	}


	// One state slot per distinct report ID of the descriptor.
	bool BuildSlots() {
		for (std::size_t index = 0; index < report.reportCount; index++) {
			uint8 reportId = report.reports[index].reportId;
			if (FindSlot(reportId) != nullptr) {
				continue;
			}

			if (slotCount == MaxReportIds) {
				return false;
			}

			slots[slotCount].reportId = reportId;
			slotCount++;
		}

		return true;
	}


	ReportSlot* FindSlot(uint8 reportId) {
		for (std::size_t index = 0; index < slotCount; index++) {
			if (slots[index].reportId == reportId) {
				return &slots[index];
			}
		}

		return nullptr;
	}


	// Reads the report type and ID of a report request's wValue. False for a type that is not input, output or feature.
	static bool ReportTypeOf(uint16 value, USBHidReportType& type) {
		uint8 wire = static_cast<uint8>(value >> 8);
		if (wire == static_cast<uint8>(USBHidReportType::Input)) {
			type = USBHidReportType::Input;
			return true;
		}

		if (wire == static_cast<uint8>(USBHidReportType::Output)) {
			type = USBHidReportType::Output;
			return true;
		}

		if (wire == static_cast<uint8>(USBHidReportType::Feature)) {
			type = USBHidReportType::Feature;
			return true;
		}

		return false;
	}


	bool BootActive() const {
		return bootSubClass && protocol == ProtocolBoot && bootLength > 0;
	}


	// Appends a whole report to the queue. The boot protocol cut is made when the report is sent (see SubmitHead), so a protocol
	// switch applies to every report that has not gone out. The caller holds the section, or is the interrupt, and checked that
	// there is room.
	void Enqueue(uint8 reportId, std::span<const uint8> data) {
		QueuedReport& entry = queue[(queueHead + queueCount) % QueueDepth];
		std::copy_n(data.begin(), data.size(), entry.data.begin());
		entry.length = data.size();
		entry.reportId = reportId;
		queueCount++;
	}


	bool QueuedFor(uint8 reportId) const {
		for (std::size_t index = 0; index < queueCount; index++) {
			if (queue[(queueHead + index) % QueueDepth].reportId == reportId) {
				return true;
			}
		}

		return false;
	}


	void ClearQueue() {
		queueHead = 0;
		queueCount = 0;
	}


	void ResetIdle() {
		for (std::size_t index = 0; index < slotCount; index++) {
			slots[index].idle = 0;
			slots[index].nextIdleTick = 0;
		}
	}


	// The head may start: configured, nothing in flight, not halted, not suspended, and a report queued. The caller holds the section,
	// or is the interrupt.
	bool CanStart() const {
		return configured && !inFlight && !inHalted && !suspended && queueCount > 0;
	}


	// Starts the head report. The caller set inFlight, so a completion inside the call finds the transfer in flight. A refused start
	// clears inFlight again, and the head stays queued.
	// The boot protocol cut is applied here, so the report that goes out is the one the current protocol asks for.
	void SubmitHead(USBDeviceContext& context) {
		const QueuedReport& head = queue[queueHead];
		std::size_t length = head.length;
		if (BootActive() && length > bootLength) {
			length = bootLength;
		}

		ResultStatus status = context.StartInTransfer(inAddress, std::span<const uint8>(head.data.data(), length), false);
		if (status != ResultStatus::ok) {
			inFlight = false;
		}
	}


	// Thread context: claims the head and starts it under the section. The start may complete the transfer inside the call, and that
	// completion runs in the same section, which nests.
	void StartFromThread(USBDeviceContext& context) {
		System::CriticalSection(true);
		if (CanStart()) {
			inFlight = true;
			SubmitHead(context);
		}

		System::CriticalSection(false);
	}


	// Asks the host for resume when a report waits while the device is suspended. The request is made once per suspension, whether the
	// core accepts it or not: the core refuses it when the host has not enabled remote wakeup, and then the report waits for the host's
	// own resume.
	void RequestWakeup(USBDeviceContext& context) {
		System::CriticalSection(true);
		bool request = configured && suspended && queueCount > 0 && !wakeRequested;
		if (request) {
			wakeRequested = true;
		}

		System::CriticalSection(false);
		if (request) {
			context.RemoteWakeup();
		}
	}


	// Queues one copy of the last input report of each ID whose idle period has passed, unless a report of that ID is queued already.
	void RepeatIdleReports() {
		System::CriticalSection(true);
		uint64 now = System::GetTick();
		if (configured) {
			for (std::size_t index = 0; index < slotCount; index++) {
				ReportSlot& slot = slots[index];
				if (slot.idle == 0 || !slot.hasInput || now < slot.nextIdleTick || queueCount == QueueDepth || QueuedFor(slot.reportId)) {
					continue;
				}

				Enqueue(slot.reportId, std::span<const uint8>(slot.input.data(), report.InputLength(slot.reportId)));
				slot.nextIdleTick = now + static_cast<uint64>(slot.idle) * IdleUnitMs;
			}
		}

		System::CriticalSection(false);
	}


	// Hands an output report to the delegate and keeps it as the last one of its ID. A report of another length is dropped.
	void DeliverOutput(std::span<const uint8> data) {
		uint8 reportId = report.UsesReportIds() ? data[0] : 0;
		if (data.size() != report.OutputLength(reportId)) {
			return;
		}

		ReportSlot* slot = FindSlot(reportId);
		if (slot != nullptr) {
			std::copy_n(data.begin(), data.size(), slot->output.begin());
		}

		if (outputDelegate) {
			outputDelegate(data);
		}
	}


	// Arms the OUT endpoint when the interface has one and it is free. The transfer is one packet long, so a report completes it:
	// a short packet ends it, and a report of exactly the packet size fills it. A completion inside the call re-arms it again.
	ResultStatus ArmOut(USBDeviceContext& context) {
		if (!configured || !hasOut || outArmed || outHalted) {
			return ResultStatus::ok;
		}

		outArmed = true;
		ResultStatus status = context.StartOutTransfer(outAddress, std::span<uint8>(outBuffer.data(), outPacketSize));
		if (status != ResultStatus::ok) {
			outArmed = false;
		}

		return status;
	}


	ResultStatus StandardRequest(const USBSetupPacket& setup, USBDeviceContext& context) {
		if (setup.request != static_cast<uint8>(USBStandardRequest::GetDescriptor) || setup.direction != USBDirection::In) {
			return ResultStatus::notSupported;
		}

		uint8 type = setup.GetDescriptorType();
		if (type == HidDescriptorType) {
			return context.ControlSendData(hidDescriptor);
		}

		if (type == ReportDescriptorType) {
			return context.ControlSendData(report.descriptor);
		}

		return ResultStatus::notSupported;
	}


	ResultStatus GetReport(const USBSetupPacket& setup, USBDeviceContext& context) {
		USBHidReportType type = USBHidReportType::Input;
		if (setup.direction != USBDirection::In || setup.length == 0 || !ReportTypeOf(setup.value, type)) {
			return ResultStatus::notSupported;
		}

		uint8 reportId = static_cast<uint8>(setup.value & 0xFF);
		std::size_t length = report.Length(type, reportId);
		if (length == 0) {
			return ResultStatus::notSupported;
		}

		if (type == USBHidReportType::Feature) {
			if (!featureGetDelegate || !featureGetDelegate(reportId, std::span<uint8>(replyBuffer.data(), length))) {
				return ResultStatus::notSupported;
			}

			return context.ControlSendData(std::span<const uint8>(replyBuffer.data(), length));
		}

		ReportSlot* slot = FindSlot(reportId);
		if (slot == nullptr) {
			return ResultStatus::notSupported;
		}

		const std::array<uint8, MaxReportLength>& source = type == USBHidReportType::Input ? slot->input : slot->output;
		std::copy_n(source.begin(), length, replyBuffer.begin());
		return context.ControlSendData(std::span<const uint8>(replyBuffer.data(), length));
	}


	ResultStatus SetReport(const USBSetupPacket& setup, USBDeviceContext& context) {
		USBHidReportType type = USBHidReportType::Input;
		if (setup.direction != USBDirection::Out || setup.length == 0 || setup.length > MaxReportLength || !ReportTypeOf(setup.value, type)) {
			return ResultStatus::notSupported;
		}

		uint8 reportId = static_cast<uint8>(setup.value & 0xFF);
		if (report.Length(type, reportId) != setup.length) {
			return ResultStatus::notSupported;
		}

		if (type == USBHidReportType::Feature && !featureSetDelegate) {
			return ResultStatus::notSupported;
		}

		return context.ControlReceiveData(std::span<uint8>(setBuffer.data(), setup.length));
	}


	ResultStatus GetIdle(const USBSetupPacket& setup, USBDeviceContext& context) {
		if (setup.direction != USBDirection::In || setup.length == 0) {
			return ResultStatus::notSupported;
		}

		ReportSlot* slot = FindSlot(static_cast<uint8>(setup.value & 0xFF));
		if (slot == nullptr) {
			return ResultStatus::notSupported;
		}

		replyBuffer[0] = slot->idle;
		return context.ControlSendData(std::span<const uint8>(replyBuffer.data(), 1));
	}


	// wValue low byte is the report ID, 0 for every report. The period is in 4 ms units, and 0 turns the repeat off.
	ResultStatus SetIdle(const USBSetupPacket& setup) {
		if (setup.direction != USBDirection::Out || setup.length != 0) {
			return ResultStatus::notSupported;
		}

		uint8 reportId = static_cast<uint8>(setup.value & 0xFF);
		uint8 period = static_cast<uint8>(setup.value >> 8);
		if (reportId == 0) {
			for (std::size_t index = 0; index < slotCount; index++) {
				ApplyIdle(slots[index], period);
			}

			return ResultStatus::ok;
		}

		ReportSlot* slot = FindSlot(reportId);
		if (slot == nullptr) {
			return ResultStatus::notSupported;
		}

		ApplyIdle(*slot, period);
		return ResultStatus::ok;
	}


	static void ApplyIdle(ReportSlot& slot, uint8 period) {
		slot.idle = period;
		slot.nextIdleTick = System::GetTick() + static_cast<uint64>(period) * IdleUnitMs;
	}


	ResultStatus GetProtocol(const USBSetupPacket& setup, USBDeviceContext& context) {
		if (!bootSubClass || setup.direction != USBDirection::In || setup.length == 0) {
			return ResultStatus::notSupported;
		}

		replyBuffer[0] = protocol;
		return context.ControlSendData(std::span<const uint8>(replyBuffer.data(), 1));
	}


	// Boot protocol (0) or report protocol (1), for a boot subclass interface only.
	ResultStatus SetProtocol(const USBSetupPacket& setup) {
		if (!bootSubClass || setup.direction != USBDirection::Out || setup.length != 0 || setup.value > 1) {
			return ResultStatus::notSupported;
		}

		protocol = static_cast<uint8>(setup.value);
		return ResultStatus::ok;
	}
};
