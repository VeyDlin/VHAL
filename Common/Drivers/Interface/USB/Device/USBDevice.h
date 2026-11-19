#pragma once
#include <Adapter/USBDeviceAdapter.h>
#include <Adapter/Utilities/USBTypes.h>
#include <Drivers/Interface/USB/Descriptor/USBDescriptorSet.h>
#include <Drivers/Interface/USB/Device/USBFunction.h>
#include <Drivers/Interface/USB/USBProtocol.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/ResultStatus.h>
#include <cstddef>
#include <span>


// Device core of the USB stack. It owns the control pipe and the standard requests, and routes class, vendor and endpoint
// traffic to the functions. The adapter reports what the hardware saw; this class decides what it means.
//
// Interrupt context: the handlers behind the adapter events run in the interrupt. They never block, allocate or enter a
// critical section. Start(), Stop() and Process() run in thread context. Stop() changes endpoint state, so the caller wraps
// it in System::CriticalSection(true) and (false). Sections nest on every port, so a caller that is already inside a section
// may still call Stop(); the core opens no section of its own.
class USBDevice : private USBDeviceContext {
private:
	// Stage of the control request in progress. Each stage ends with exactly one completion of endpoint 0.
	enum class ControlStage : uint8 {
		Idle,
		DataIn,
		StatusOut,
		DataOut,
		StatusIn
	};


	static constexpr uint8 DeviceDescriptorSize = 18;
	// Device descriptor byte 16, iSerialNumber: the string index the serial number is answered at.
	static constexpr uint8 DeviceSerialNumberIndex = 16;
	static constexpr uint8 ConfigurationHeaderSize = 9;
	static constexpr uint8 BosHeaderSize = 5;
	static constexpr uint8 MaxDeviceAddress = 127;
	static constexpr uint8 ConfigurationValueIndex = 5;
	static constexpr uint8 ConfigurationAttributesIndex = 7;
	// bmAttributes bit 6 of the configuration descriptor: the device is self-powered.
	static constexpr uint8 SelfPoweredAttribute = 0x40;
	// bmAttributes bit 5 of the configuration descriptor: the device may ask for remote wakeup. SET_FEATURE needs it.
	static constexpr uint8 RemoteWakeupAttribute = 0x20;
	// GET_STATUS(DEVICE) bits: bit 0 self-powered, bit 1 remote wakeup enabled by the host (USB 2.0 section 9.4.5).
	static constexpr uint8 SelfPoweredStatus = 0x01;
	static constexpr uint8 RemoteWakeupStatus = 0x02;
	USBDeviceAdapter& adapter;
	// A copy of the set. Its spans are kept, so the arrays behind them must outlive the device.
	USBDescriptorSet descriptorSet;
	// The complete serial number string from SetSerialNumber(). Empty when none was given. Its array must have static storage too.
	std::span<const uint8> runtimeSerialNumber;
	std::span<USBFunction* const> functionList;
	USBDeviceState state = USBDeviceState::Detached;
	USBDeviceState stateBeforeSuspend = USBDeviceState::Default;
	uint8 address = 0;
	uint8 pendingAddress = 0;
	// Set between SET_ADDRESS and its status completion. The address takes effect only then (see USBDeviceAdapter::SetAddress).
	bool addressPending = false;
	uint8 configurationValue = 0;
	bool remoteWakeupEnabled = false;
	bool started = false;
	// The six adapter events are subscribed once, on the first Start(), and stay subscribed (see SubscribeEvents()).
	bool eventsSubscribed = false;
	uint32 errorCount = 0;
	ControlStage stage = ControlStage::Idle;
	// The request in progress, from SETUP until its status stage completes. The OUT data stage completion needs it.
	USBSetupPacket controlRequest;
	// The function that answers controlRequest. nullptr for a request the core answers itself.
	USBFunction* requestOwner = nullptr;
	// True only while OnControlRequest() runs, the only time ControlSendData() and ControlReceiveData() are allowed.
	bool answering = false;
	// Set once the request has its data stage, so the core does not add a status stage or a stall on top of it.
	bool dataStageStarted = false;
	std::span<uint8> controlReceiveBuffer;
	// Answers the core builds itself (GET_STATUS, GET_CONFIGURATION, GET_INTERFACE). They live in the object, not on the
	// stack, because the adapter reads them until the stage completes.
	uint8 answerBytes[2] = {};


public:
	USBDevice(USBDeviceAdapter& deviceAdapter, const USBDescriptorSet& descriptors, std::span<USBFunction* const> functions)
		: adapter(deviceAdapter),
		  descriptorSet(descriptors),
		  functionList(functions) {
	}


	// A copy would share the adapter's event subscriptions with the original, so the device cannot be copied.
	USBDevice(const USBDevice&) = delete;

	USBDevice& operator=(const USBDevice&) = delete;


	// Clears the six event captures of this device, so an event the adapter raises later cannot call into a destroyed
	// object. Stop() must have run and the USB interrupt must be disabled before the object is destroyed: the destructor
	// does not touch the hardware and cannot enter a critical section.
	~USBDevice() {
		adapter.onBusReset = nullptr;
		adapter.onSetup = nullptr;
		adapter.onTransferComplete = nullptr;
		adapter.onSuspend = nullptr;
		adapter.onResume = nullptr;
		adapter.onError = nullptr;
	}


	// Validates the descriptor set and the functions, applies the parameters, subscribes to the adapter events and
	// connects. A board with a fixed pull-up reports notSupported from Connect(), which is success here.
	ResultStatus Start(const USBDeviceAdapter::Parameters& parameters) {
		if (started) {
			return ResultStatus::busy;
		}

		if (!IsDescriptorSetValid(descriptorSet) || !AreFunctionsValid() || !IsSerialNumberValid()) {
			return ResultStatus::invalidParameter;
		}

		ResultStatus status = adapter.SetParameters(parameters);
		if (status != ResultStatus::ok) {
			return status;
		}

		ResetState();
		errorCount = 0;
		if (!eventsSubscribed) {
			SubscribeEvents();
			eventsSubscribed = true;
		}

		// Set before Connect(), so a bus reset raised by the connection is not dropped. Rolled back if the connection fails.
		started = true;
		status = adapter.Connect();
		if (status == ResultStatus::notSupported) {
			status = ResultStatus::ok;
		}

		if (status != ResultStatus::ok) {
			started = false;
			ResetState();
			return status;
		}

		return ResultStatus::ok;
	}


	// Supplies the complete string descriptor of iSerialNumber at run time, for a serial number known only on the device, such as
	// the MCU's unique ID (MakeUSBSerialNumberDescriptor() builds it). Thread context, before Start(). The span is kept, not
	// copied, so its array must have static storage. busy once the device is started. invalidParameter unless the set reserves
	// the run-time serial number (SerialNumberAtRuntime()), so a static serial is never replaced silently, and unless the
	// descriptor is a string descriptor: bLength equal to its size, bDescriptorType 3, an even size, and at least one character
	// (4 bytes), so an empty serial is refused.
	ResultStatus SetSerialNumber(std::span<const uint8> descriptor) {
		if (started) {
			return ResultStatus::busy;
		}

		if (!descriptorSet.serialNumberAtRuntime) {
			return ResultStatus::invalidParameter;
		}

		if (descriptor.size() < 4 || descriptor.size() % 2 != 0
			|| static_cast<std::size_t>(descriptor[0]) != descriptor.size()
			|| descriptor[1] != static_cast<uint8>(USBDescriptorType::String)) {
			return ResultStatus::invalidParameter;
		}

		runtimeSerialNumber = descriptor;
		return ResultStatus::ok;
	}


	// Detaches from the bus. The caller wraps this in a critical section, see the class comment.
	void Stop() {
		if (!started) {
			return;
		}

		Deconfigure();
		adapter.Disconnect();
		started = false;
		ResetState();
	}


	// Deferred work of the functions, from the application's main loop or thread.
	void Process() {
		for (std::size_t index = 0; index < functionList.size(); ++index) {
			functionList[index]->Process(*this);
		}
	}


	USBDeviceState GetState() const {
		return state;
	}


	uint8 GetAddress() const {
		return address;
	}


	uint8 GetConfigurationValue() const {
		return configurationValue;
	}


	bool IsRemoteWakeupEnabled() const {
		return remoteWakeupEnabled;
	}


	uint32 GetErrorCount() const {
		return errorCount;
	}


	// Also the RemoteWakeup() of USBDeviceContext, so a function asks through the same gate.
	ResultStatus RemoteWakeup() override {
		if (state != USBDeviceState::Suspended) {
			return ResultStatus::notReady;
		}

		if (!remoteWakeupEnabled) {
			return ResultStatus::notSupported;
		}

		return adapter.RemoteWakeup();
	}


private:
	// USBDeviceContext. The functions reach these only through the context reference they are given.
	ResultStatus OpenEndpoint(const USBDeviceAdapter::EndpointConfig& config) override {
		return adapter.OpenEndpoint(config);
	}


	ResultStatus CloseEndpoint(uint8 endpointAddress) override {
		return adapter.CloseEndpoint(endpointAddress);
	}


	ResultStatus StartInTransfer(uint8 endpointAddress, std::span<const uint8> data, bool sendZeroLengthPacket) override {
		return adapter.StartInTransfer(endpointAddress, data, sendZeroLengthPacket);
	}


	// The adapter may complete the transfer inside this call, before it returns (see USBDeviceAdapter::StartOutTransfer()).
	ResultStatus StartOutTransfer(uint8 endpointAddress, std::span<uint8> buffer) override {
		return adapter.StartOutTransfer(endpointAddress, buffer);
	}


	ResultStatus Stall(uint8 endpointAddress) override {
		return SetEndpointHalt(endpointAddress, true);
	}


	ResultStatus ClearStall(uint8 endpointAddress) override {
		return SetEndpointHalt(endpointAddress, false);
	}


	ResultStatus ControlSendData(std::span<const uint8> data) override {
		if (!answering) {
			return ResultStatus::notReady;
		}

		if (controlRequest.length == 0 || controlRequest.direction != USBDirection::In) {
			return ResultStatus::invalidParameter;
		}

		if (dataStageStarted) {
			return ResultStatus::busy;
		}

		// Set before the transfer starts, so a completion that runs inside the call sees the stage as started.
		dataStageStarted = true;
		ResultStatus status = StartDataIn(data);
		if (status != ResultStatus::ok) {
			dataStageStarted = false;
			stage = ControlStage::Idle;
		}

		return status;
	}


	ResultStatus ControlReceiveData(std::span<uint8> buffer) override {
		if (!answering) {
			return ResultStatus::notReady;
		}

		if (controlRequest.length == 0 || controlRequest.direction != USBDirection::Out) {
			return ResultStatus::invalidParameter;
		}

		if (dataStageStarted) {
			return ResultStatus::busy;
		}

		std::size_t requested = controlRequest.length;
		if (buffer.size() < requested) {
			return ResultStatus::invalidParameter;
		}

		std::span<uint8> receive = buffer.first(requested);
		controlReceiveBuffer = receive;
		dataStageStarted = true;
		stage = ControlStage::DataOut;
		ResultStatus status = adapter.StartOutTransfer(USBEndpoint::ControlOut, receive);
		// A transfer that completed inside the call has already moved the stage on (to the status stage), so only a refused
		// transfer is rolled back here.
		if (status != ResultStatus::ok && stage == ControlStage::DataOut) {
			dataStageStarted = false;
			stage = ControlStage::Idle;
			controlReceiveBuffer = std::span<uint8>();
		}

		return status;
	}


	// Subscribes the six events the core owns, once, before the first Connect(). They are assigned here and never replaced
	// while the device is alive, because a std::function the interrupt may be running must not be replaced. Only the
	// destructor clears them, under the precondition stated there. The handlers ignore events while the device is stopped.
	void SubscribeEvents() {
		adapter.onBusReset = [this](USBSpeed) {
			HandleBusReset();
		};

		adapter.onSetup = [this](const USBSetupBytes& setup) {
			HandleSetup(setup);
		};

		adapter.onTransferComplete = [this](uint8 endpointAddress, uint32 byteCount) {
			HandleTransferComplete(endpointAddress, byteCount);
		};

		adapter.onSuspend = [this]() {
			HandleSuspend();
		};

		adapter.onResume = [this]() {
			HandleResume();
		};

		adapter.onError = [this](USBDeviceAdapter::Error error) {
			HandleError(error);
		};
	}


	void HandleBusReset() {
		if (!started) {
			return;
		}

		adapter.CloseAllEndpoints();
		for (std::size_t index = 0; index < functionList.size(); ++index) {
			functionList[index]->OnBusReset();
		}

		state = USBDeviceState::Default;
		address = 0;
		configurationValue = 0;
		remoteWakeupEnabled = false;
		addressPending = false;
		EndRequest();
	}


	void HandleSetup(const USBSetupBytes& setup) {
		if (!started) {
			return;
		}

		// A new SETUP abandons whatever the previous request was doing, including a SET_ADDRESS whose status never came.
		addressPending = false;
		EndRequest();
		controlRequest = USBSetupPacket::Parse(setup);
		DispatchRequest();
	}


	void HandleTransferComplete(uint8 endpointAddress, uint32 byteCount) {
		if (!started) {
			return;
		}

		if (endpointAddress == USBEndpoint::ControlIn) {
			HandleControlInCompletion();
			return;
		}

		if (endpointAddress == USBEndpoint::ControlOut) {
			HandleControlOutCompletion(byteCount);
			return;
		}

		USBFunction* owner = FindEndpointOwner(endpointAddress);
		if (owner != nullptr) {
			owner->OnTransferComplete(endpointAddress, byteCount, *this);
		}
	}


	void HandleControlInCompletion() {
		if (stage == ControlStage::DataIn) {
			if (StartStatusOut() != ResultStatus::ok) {
				StallControl();
			}

			return;
		}

		if (stage == ControlStage::StatusIn) {
			CommitAddress();
			EndRequest();
		}
	}


	void HandleControlOutCompletion(uint32 byteCount) {
		if (stage == ControlStage::StatusOut) {
			EndRequest();
			return;
		}

		if (stage == ControlStage::DataOut) {
			HandleDataOutCompletion(byteCount);
		}
	}


	void HandleDataOutCompletion(uint32 byteCount) {
		std::size_t received = byteCount;
		if (requestOwner == nullptr || received > controlReceiveBuffer.size()) {
			StallControl();
			return;
		}

		std::span<const uint8> data = controlReceiveBuffer.first(received);
		if (requestOwner->OnControlDataReceived(controlRequest, data, *this) != ResultStatus::ok) {
			StallControl();
			return;
		}

		CompleteWithStatusIn();
	}


	void HandleSuspend() {
		if (!started) {
			return;
		}

		if (state != USBDeviceState::Default && state != USBDeviceState::Address && state != USBDeviceState::Configured) {
			return;
		}

		stateBeforeSuspend = state;
		state = USBDeviceState::Suspended;
		for (std::size_t index = 0; index < functionList.size(); ++index) {
			functionList[index]->OnSuspend();
		}
	}


	void HandleResume() {
		if (!started) {
			return;
		}

		if (state != USBDeviceState::Suspended) {
			return;
		}

		state = stateBeforeSuspend;
		for (std::size_t index = 0; index < functionList.size(); ++index) {
			functionList[index]->OnResume();
		}
	}


	// Errors are counted. A DataOverrun in the OUT data stage stalls the request. If a completion still arrives after the
	// stall, the stage is Idle and it is ignored.
	void HandleError(USBDeviceAdapter::Error error) {
		if (!started) {
			return;
		}

		++errorCount;
		if (error == USBDeviceAdapter::Error::DataOverrun && stage == ControlStage::DataOut) {
			StallControl();
		}
	}


	void DispatchRequest() {
		if (controlRequest.type == USBRequestType::Reserved || !HasValidStandardDirection()) {
			StallControl();
			return;
		}

		// USB 2.0 section 9.4: interfaces and endpoints other than 0 exist only in the Configured state. A request to one of them
		// in the Default or Address state is a request error, whatever the request is, so no function sees it.
		if (IsNonControlRecipient() && !IsConfiguredForRequests()) {
			StallControl();
			return;
		}

		if (controlRequest.recipient == USBRecipient::Device) {
			DispatchDeviceRequest();
			return;
		}

		if (controlRequest.recipient == USBRecipient::Interface) {
			DispatchInterfaceRequest();
			return;
		}

		if (controlRequest.recipient == USBRecipient::Endpoint) {
			DispatchEndpointRequest();
			return;
		}

		StallControl();
	}


	void DispatchDeviceRequest() {
		if (controlRequest.type == USBRequestType::Vendor) {
			if (controlRequest.direction == USBDirection::In) {
				AnswerVendorRequest();
				return;
			}

			StallControl();
			return;
		}

		if (controlRequest.type != USBRequestType::Standard) {
			StallControl();
			return;
		}

		if (MatchesStandard(USBStandardRequest::GetStatus)) {
			AnswerDeviceStatus();
			return;
		}

		if (MatchesStandard(USBStandardRequest::SetAddress)) {
			SetDeviceAddress();
			return;
		}

		if (MatchesStandard(USBStandardRequest::GetDescriptor)) {
			AnswerDescriptor();
			return;
		}

		if (MatchesStandard(USBStandardRequest::SetConfiguration)) {
			SetDeviceConfiguration();
			return;
		}

		if (MatchesStandard(USBStandardRequest::GetConfiguration)) {
			AnswerConfiguration();
			return;
		}

		if (MatchesStandard(USBStandardRequest::SetFeature)) {
			SetDeviceFeature(true);
			return;
		}

		if (MatchesStandard(USBStandardRequest::ClearFeature)) {
			SetDeviceFeature(false);
			return;
		}

		StallControl();
	}


	void DispatchInterfaceRequest() {
		uint8 interfaceNumber = controlRequest.GetIndexLow();
		USBFunction* owner = FindInterfaceOwner(interfaceNumber);

		if (MatchesStandard(USBStandardRequest::GetStatus)) {
			AnswerInterfaceStatus(owner);
			return;
		}

		if (MatchesStandard(USBStandardRequest::GetInterface)) {
			AnswerInterface(owner, interfaceNumber);
			return;
		}

		if (MatchesStandard(USBStandardRequest::SetInterface)) {
			SetInterfaceAlternate(owner, interfaceNumber);
			return;
		}

		RouteToFunction(owner);
	}


	void DispatchEndpointRequest() {
		uint8 endpointAddress = controlRequest.GetIndexLow();
		USBFunction* owner = FindEndpointOwner(endpointAddress);
		bool known = USBEndpoint::IsControl(endpointAddress) || owner != nullptr;

		if (MatchesStandard(USBStandardRequest::GetStatus)) {
			AnswerEndpointStatus(endpointAddress, known);
			return;
		}

		bool isHaltFeature = controlRequest.value == static_cast<uint16>(USBFeature::EndpointHalt);
		if (isHaltFeature && MatchesStandard(USBStandardRequest::SetFeature)) {
			SetEndpointHaltRequest(endpointAddress, owner, true);
			return;
		}

		if (isHaltFeature && MatchesStandard(USBStandardRequest::ClearFeature)) {
			SetEndpointHaltRequest(endpointAddress, owner, false);
			return;
		}

		RouteToFunction(owner);
	}


	void AnswerVendorRequest() {
		for (const USBVendorDescriptor& entry : descriptorSet.vendorRequests) {
			bool valueMatches = !entry.matchValue || entry.value == controlRequest.value;
			if (entry.request == controlRequest.request && entry.index == controlRequest.index && valueMatches) {
				AnswerIn(entry.bytes);
				return;
			}
		}

		StallControl();
	}


	void AnswerDeviceStatus() {
		bool selfPowered = (descriptorSet.configuration[ConfigurationAttributesIndex] & SelfPoweredAttribute) != 0;
		uint8 status = 0;
		if (selfPowered) {
			status = static_cast<uint8>(status | SelfPoweredStatus);
		}

		if (remoteWakeupEnabled) {
			status = static_cast<uint8>(status | RemoteWakeupStatus);
		}

		answerBytes[0] = status;
		answerBytes[1] = 0;
		AnswerIn(std::span<const uint8>(answerBytes));
	}


	void AnswerDescriptor() {
		uint8 descriptorType = controlRequest.GetDescriptorType();
		uint8 descriptorIndex = controlRequest.GetDescriptorIndex();

		if (descriptorType == static_cast<uint8>(USBDescriptorType::Device)) {
			AnswerIn(descriptorSet.device);
			return;
		}

		if (descriptorType == static_cast<uint8>(USBDescriptorType::Configuration) && descriptorIndex == 0) {
			AnswerIn(descriptorSet.configuration);
			return;
		}

		// Index 0 is the language list whatever the device descriptor says, so the serial number is never answered there.
		bool serialNumberIndex = descriptorIndex != 0 && descriptorIndex == descriptorSet.device[DeviceSerialNumberIndex];
		if (descriptorType == static_cast<uint8>(USBDescriptorType::String) && serialNumberIndex && !runtimeSerialNumber.empty()) {
			AnswerIn(runtimeSerialNumber);
			return;
		}

		if (descriptorType == static_cast<uint8>(USBDescriptorType::String)
			&& static_cast<std::size_t>(descriptorIndex) < descriptorSet.strings.size()) {
			AnswerIn(descriptorSet.strings[descriptorIndex]);
			return;
		}

		if (descriptorType == static_cast<uint8>(USBDescriptorType::BOS) && !descriptorSet.bos.empty()) {
			AnswerIn(descriptorSet.bos);
			return;
		}

		// Device qualifier and other-speed configuration describe a second speed, which this full-speed device does not have.
		StallControl();
	}


	void AnswerConfiguration() {
		USBDeviceState effective = EffectiveState();
		if (effective != USBDeviceState::Address && effective != USBDeviceState::Configured) {
			StallControl();
			return;
		}

		answerBytes[0] = configurationValue;
		AnswerIn(std::span<const uint8>(answerBytes, 1));
	}


	void AnswerInterfaceStatus(USBFunction* owner) {
		if (owner == nullptr) {
			StallControl();
			return;
		}

		answerBytes[0] = 0;
		answerBytes[1] = 0;
		AnswerIn(std::span<const uint8>(answerBytes));
	}


	void AnswerInterface(USBFunction* owner, uint8 interfaceNumber) {
		if (owner == nullptr || EffectiveState() != USBDeviceState::Configured) {
			StallControl();
			return;
		}

		answerBytes[0] = owner->GetInterface(interfaceNumber);
		AnswerIn(std::span<const uint8>(answerBytes, 1));
	}


	void AnswerEndpointStatus(uint8 endpointAddress, bool known) {
		if (!known) {
			StallControl();
			return;
		}

		answerBytes[0] = static_cast<uint8>(adapter.IsStalled(endpointAddress) ? 1 : 0);
		answerBytes[1] = 0;
		AnswerIn(std::span<const uint8>(answerBytes));
	}


	void SetDeviceAddress() {
		USBDeviceState effective = EffectiveState();
		bool stateAllows = effective == USBDeviceState::Default || effective == USBDeviceState::Address;
		if (!stateAllows || controlRequest.index != 0 || controlRequest.length != 0 || controlRequest.value > MaxDeviceAddress) {
			StallControl();
			return;
		}

		uint8 newAddress = static_cast<uint8>(controlRequest.value);
		if (adapter.SetAddress(newAddress) != ResultStatus::ok) {
			StallControl();
			return;
		}

		pendingAddress = newAddress;
		addressPending = true;
		if (StartStatusIn() != ResultStatus::ok) {
			// The adapter now holds the new address pending and would apply it at the next status completion, which is not
			// this request's. Re-pend the current address before the stall, so the hardware keeps the address it has.
			adapter.SetAddress(address);
			StallControl();
		}
	}


	void SetDeviceConfiguration() {
		USBDeviceState effective = EffectiveState();
		if (effective != USBDeviceState::Address && effective != USBDeviceState::Configured) {
			StallControl();
			return;
		}

		if (controlRequest.value == 0) {
			Deconfigure();
			CompleteWithStatusIn();
			return;
		}

		uint8 descriptorValue = descriptorSet.configuration[ConfigurationValueIndex];
		if (controlRequest.value != descriptorValue) {
			StallControl();
			return;
		}

		Deconfigure();
		if (!ConfigureFunctions()) {
			StallControl();
			return;
		}

		configurationValue = descriptorValue;
		SetEffectiveState(USBDeviceState::Configured);
		if (StartStatusIn() != ResultStatus::ok) {
			// The status stage did not start, so the configuration is undone: the device must not stay configured with
			// its endpoints open behind a stall.
			Deconfigure();
			StallControl();
		}
	}


	void SetDeviceFeature(bool set) {
		// The device form has wIndex 0 and no data stage (USB 2.0 sections 9.4.1 and 9.4.9).
		if (controlRequest.value != static_cast<uint16>(USBFeature::DeviceRemoteWakeup)
			|| controlRequest.index != 0
			|| controlRequest.length != 0) {
			StallControl();
			return;
		}

		// The host may enable remote wakeup only when the configuration advertises it. Clearing it needs no such check.
		if (set && !IsRemoteWakeupAdvertised()) {
			StallControl();
			return;
		}

		// The flag changes only once the status stage has started. A refused stage then leaves the device as it was.
		if (StartStatusIn() != ResultStatus::ok) {
			StallControl();
			return;
		}

		remoteWakeupEnabled = set;
	}


	bool IsRemoteWakeupAdvertised() const {
		return (descriptorSet.configuration[ConfigurationAttributesIndex] & RemoteWakeupAttribute) != 0;
	}


	void SetEndpointHaltRequest(uint8 endpointAddress, USBFunction* owner, bool halted) {
		// The endpoint form has no data stage (USB 2.0 sections 9.4.1 and 9.4.9). wIndex is the endpoint itself.
		if (controlRequest.length != 0) {
			StallControl();
			return;
		}

		// Endpoint 0 has no halt to change here; the request is acknowledged.
		if (USBEndpoint::IsControl(endpointAddress)) {
			CompleteWithStatusIn();
			return;
		}

		if (owner == nullptr) {
			StallControl();
			return;
		}

		if (SetEndpointHalt(endpointAddress, halted) != ResultStatus::ok) {
			StallControl();
			return;
		}

		CompleteWithStatusIn();
	}


	void SetInterfaceAlternate(USBFunction* owner, uint8 interfaceNumber) {
		if (owner == nullptr || EffectiveState() != USBDeviceState::Configured || controlRequest.value > 0xFF) {
			StallControl();
			return;
		}

		if (owner->OnSetInterface(interfaceNumber, static_cast<uint8>(controlRequest.value), *this) != ResultStatus::ok) {
			StallControl();
			return;
		}

		CompleteWithStatusIn();
	}


	// Hands a request to the function that owns its recipient. Its answer decides the rest of the request: a data stage it
	// started is completed by the events, a request without data gets a status stage, anything else is stalled.
	void RouteToFunction(USBFunction* owner) {
		if (owner == nullptr) {
			StallControl();
			return;
		}

		requestOwner = owner;
		answering = true;
		ResultStatus status = owner->OnControlRequest(controlRequest, *this);
		answering = false;
		if (status != ResultStatus::ok) {
			StallControl();
			return;
		}

		if (dataStageStarted) {
			return;
		}

		if (controlRequest.length == 0) {
			CompleteWithStatusIn();
			return;
		}

		StallControl();
	}


	// Sets or clears the halt and informs the owner of a non-control endpoint, which has no transfer left to finish.
	ResultStatus SetEndpointHalt(uint8 endpointAddress, bool halted) {
		ResultStatus status = halted ? adapter.Stall(endpointAddress) : adapter.ClearStall(endpointAddress);
		if (status != ResultStatus::ok) {
			return status;
		}

		if (!USBEndpoint::IsControl(endpointAddress)) {
			USBFunction* owner = FindEndpointOwner(endpointAddress);
			if (owner != nullptr) {
				owner->OnEndpointHaltChanged(endpointAddress, halted, *this);
			}
		}

		return ResultStatus::ok;
	}


	void CommitAddress() {
		if (!addressPending) {
			return;
		}

		USBDeviceState next = pendingAddress == 0 ? USBDeviceState::Default : USBDeviceState::Address;
		address = pendingAddress;
		addressPending = false;
		// A suspend that landed while the status stage was pending keeps Suspended. The state it returns to on resume is the
		// new one, so OnResume() still reaches the functions.
		SetEffectiveState(next);
	}


	// Leaves the configuration: functions drop their work, then the endpoints close. Endpoint 0 stays open.
	void Deconfigure() {
		if (configurationValue != 0) {
			for (std::size_t index = 0; index < functionList.size(); ++index) {
				functionList[index]->OnDeconfigured();
			}
		}

		adapter.CloseAllEndpoints();
		configurationValue = 0;
		if (EffectiveState() == USBDeviceState::Configured) {
			SetEffectiveState(USBDeviceState::Address);
		}
	}


	// Configures the functions in order. A failure undoes the ones already configured and leaves the device unconfigured.
	bool ConfigureFunctions() {
		for (std::size_t index = 0; index < functionList.size(); ++index) {
			if (functionList[index]->OnConfigured(*this) != ResultStatus::ok) {
				UndoConfiguration(index);
				return false;
			}
		}

		return true;
	}


	void UndoConfiguration(std::size_t configuredCount) {
		for (std::size_t index = 0; index < configuredCount; ++index) {
			functionList[index]->OnDeconfigured();
		}

		adapter.CloseAllEndpoints();
	}


	// Starts the IN data stage with data cut to wLength. A short answer asks the adapter for a zero-length packet, and the
	// adapter sends it only when the answer is a whole number of packets, the USB rule for ending an IN data stage. A request
	// that asks for no data cannot take an answer: that is a stall.
	ResultStatus StartDataIn(std::span<const uint8> data) {
		std::size_t requested = controlRequest.length;
		std::size_t count = data.size() < requested ? data.size() : requested;
		stage = ControlStage::DataIn;
		return adapter.StartInTransfer(USBEndpoint::ControlIn, data.first(count), count < requested);
	}


	void AnswerIn(std::span<const uint8> data) {
		if (controlRequest.length == 0 || StartDataIn(data) != ResultStatus::ok) {
			StallControl();
		}
	}


	ResultStatus StartStatusIn() {
		stage = ControlStage::StatusIn;
		return adapter.StartInTransfer(USBEndpoint::ControlIn, std::span<const uint8>(), false);
	}


	ResultStatus StartStatusOut() {
		stage = ControlStage::StatusOut;
		return adapter.StartOutTransfer(USBEndpoint::ControlOut, std::span<uint8>());
	}


	void CompleteWithStatusIn() {
		if (StartStatusIn() != ResultStatus::ok) {
			StallControl();
		}
	}


	// Stalls the control pipe in both directions and ends the request. A pending address is dropped with it.
	void StallControl() {
		adapter.Stall(USBEndpoint::ControlIn);
		addressPending = false;
		EndRequest();
	}


	// The state of a device that is not attached. The endpoint state is left to the caller.
	void ResetState() {
		state = USBDeviceState::Detached;
		address = 0;
		configurationValue = 0;
		remoteWakeupEnabled = false;
		addressPending = false;
		EndRequest();
	}


	void EndRequest() {
		stage = ControlStage::Idle;
		requestOwner = nullptr;
		dataStageStarted = false;
		controlReceiveBuffer = std::span<uint8>();
	}


	bool MatchesStandard(USBStandardRequest request) const {
		return controlRequest.type == USBRequestType::Standard
			&& controlRequest.request == static_cast<uint8>(request);
	}


	// USB 2.0 section 9.3 fixes the direction of each standard request. A request sent in the other direction is malformed.
	bool HasValidStandardDirection() const {
		if (controlRequest.type != USBRequestType::Standard) {
			return true;
		}

		bool inRequest = MatchesStandard(USBStandardRequest::GetStatus)
			|| MatchesStandard(USBStandardRequest::GetDescriptor)
			|| MatchesStandard(USBStandardRequest::GetConfiguration)
			|| MatchesStandard(USBStandardRequest::GetInterface);
		if (inRequest) {
			return controlRequest.direction == USBDirection::In;
		}

		bool outRequest = MatchesStandard(USBStandardRequest::SetAddress)
			|| MatchesStandard(USBStandardRequest::SetConfiguration)
			|| MatchesStandard(USBStandardRequest::SetFeature)
			|| MatchesStandard(USBStandardRequest::ClearFeature)
			|| MatchesStandard(USBStandardRequest::SetInterface);
		if (outRequest) {
			return controlRequest.direction == USBDirection::Out;
		}

		return true;
	}


	// The state every request is answered in. A suspended device is answered as the state it returns to on resume, so a SETUP
	// that arrives while a configured device is suspended is not refused as if it were unconfigured. Every handler and the
	// gate read this one helper, so they cannot disagree about a suspended device.
	USBDeviceState EffectiveState() const {
		return state == USBDeviceState::Suspended ? stateBeforeSuspend : state;
	}


	// Writes the state the requests are answered in. While suspended, the new state is the one resume returns to.
	void SetEffectiveState(USBDeviceState next) {
		if (state == USBDeviceState::Suspended) {
			stateBeforeSuspend = next;
		} else {
			state = next;
		}
	}


	bool IsConfiguredForRequests() const {
		return EffectiveState() == USBDeviceState::Configured;
	}


	// An interface recipient, or an endpoint recipient other than endpoint 0.
	bool IsNonControlRecipient() const {
		if (controlRequest.recipient == USBRecipient::Interface) {
			return true;
		}

		return controlRequest.recipient == USBRecipient::Endpoint && !USBEndpoint::IsControl(controlRequest.GetIndexLow());
	}


	USBFunction* FindInterfaceOwner(uint8 interfaceNumber) const {
		for (std::size_t index = 0; index < functionList.size(); ++index) {
			USBFunction* function = functionList[index];
			if (function->OwnsInterface(interfaceNumber)) {
				return function;
			}
		}

		return nullptr;
	}


	USBFunction* FindEndpointOwner(uint8 endpointAddress) const {
		for (std::size_t index = 0; index < functionList.size(); ++index) {
			USBFunction* function = functionList[index];
			if (function->OwnsEndpoint(endpointAddress)) {
				return function;
			}
		}

		return nullptr;
	}


	bool AreFunctionsValid() const {
		for (std::size_t index = 0; index < functionList.size(); ++index) {
			if (functionList[index] == nullptr) {
				return false;
			}
		}

		return true;
	}


	// A set that reserves iSerialNumber needs a serial number string from SetSerialNumber(), and a non-zero index to answer it at.
	// Called after IsDescriptorSetValid(), so the device descriptor has its size.
	bool IsSerialNumberValid() const {
		if (!descriptorSet.serialNumberAtRuntime) {
			return true;
		}

		return descriptorSet.device[DeviceSerialNumberIndex] != 0 && !runtimeSerialNumber.empty();
	}


	// Reads a little-endian 16-bit field. The caller checks that both bytes exist.
	static uint16 ReadUInt16(std::span<const uint8> bytes, std::size_t offset) {
		return static_cast<uint16>(bytes[offset] | (bytes[offset + 1] << 8));
	}


	static bool IsDescriptorSetValid(const USBDescriptorSet& set) {
		if (!IsDeviceDescriptorValid(set.device) || !IsConfigurationDescriptorValid(set.configuration)) {
			return false;
		}

		if (!IsBosDescriptorValid(set.bos)) {
			return false;
		}

		// Hosts ask for the BOS only from bcdUSB 2.01 on, so a BOS behind 2.00 would never be read.
		if (!set.bos.empty() && ReadUInt16(set.device, 2) < 0x0201) {
			return false;
		}

		for (const std::span<const uint8>& descriptor : set.strings) {
			if (!IsStringDescriptorValid(descriptor)) {
				return false;
			}
		}

		return true;
	}


	static bool IsDeviceDescriptorValid(std::span<const uint8> device) {
		return device.size() == DeviceDescriptorSize
			&& device[0] == DeviceDescriptorSize
			&& device[1] == static_cast<uint8>(USBDescriptorType::Device)
			&& device[7] == USBDeviceAdapter::ControlMaxPacketSize;
	}


	// wTotalLength must match the size, and bConfigurationValue must not be 0 (0 means not configured).
	static bool IsConfigurationDescriptorValid(std::span<const uint8> configuration) {
		if (configuration.size() < ConfigurationHeaderSize) {
			return false;
		}

		if (configuration[1] != static_cast<uint8>(USBDescriptorType::Configuration)) {
			return false;
		}

		std::size_t totalLength = ReadUInt16(configuration, 2);
		return totalLength == configuration.size() && configuration[ConfigurationValueIndex] != 0;
	}


	static bool IsBosDescriptorValid(std::span<const uint8> bos) {
		if (bos.empty()) {
			return true;
		}

		// ReadUInt16 needs the header bytes, so the size is checked before wTotalLength is read.
		if (bos.size() < BosHeaderSize) {
			return false;
		}

		std::size_t totalLength = ReadUInt16(bos, 2);
		return bos[1] == static_cast<uint8>(USBDescriptorType::BOS)
			&& totalLength == bos.size();
	}


	// bLength must equal the size, and the type must be string.
	static bool IsStringDescriptorValid(std::span<const uint8> descriptor) {
		if (descriptor.size() < 2) {
			return false;
		}

		return static_cast<std::size_t>(descriptor[0]) == descriptor.size()
			&& descriptor[1] == static_cast<uint8>(USBDescriptorType::String);
	}
};
