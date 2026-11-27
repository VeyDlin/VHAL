#pragma once
#include <array>
#include <cstddef>
#include <span>
#include <Drivers/Interface/USB/Class/DFU/IDfuTarget.h>
#include <Drivers/Interface/USB/Class/DFU/USBDfuProtocol.h>
#include <Drivers/Interface/USB/Device/USBFunction.h>
#include <System/System.h>
#include <Utilities/Result/Result.h>
#include <Utilities/Callback/Delegate.h>


// The DFU 1.1 mode function (DFU 1.1 sections 6 and 7, appendix A). It runs the state machine over an IDfuTarget. The states,
// the answers and the status codes are the specification's; the transitions are in the comments of the handlers.
//
// The control pipe is the only pipe. Requests are handled in interrupt context:
//   DFU_DNLOAD (wLength > 0) receives the block into block with ControlReceiveData; the block is then pending, the state is
//                            dfuDNLOAD-SYNC, and the write waits for DFU_GETSTATUS.
//   DFU_GETSTATUS            starts the write (dfuDNBUSY, with the target's poll timeout) when a block is pending, and reports
//                            the state the device goes to next.
//   DFU_UPLOAD               reads the block from the target and sends it with ControlSendData. A block shorter than the request
//                            is the end of the upload: dfuIDLE.
//   DFU_DNLOAD (wLength = 0) starts the manifestation (dfuMANIFEST-SYNC) when the target reports IsComplete().
//
// Process() does the slow work, in thread context: the target's Write() in dfuDNBUSY and Manifest() in dfuMANIFEST. While that
// work runs the target is busy: a DFU_DNLOAD or DFU_UPLOAD stalls, even after a bus reset, so no new transfer touches the memory
// or the block buffer the work reads. A result that comes back after the device left the state it started in is dropped, so a
// write that finishes after a reset, or after a stall, does not change the state.
//
// Answers while a write or a manifestation is running: DFU_GETSTATE stalls and moves to dfuERROR, and DFU_DNLOAD and DFU_UPLOAD
// stall the same way, as appendix A.2.5 and A.2.7 say (any class request in those states stalls). DFU_GETSTATUS is the one
// deliberate difference: it answers the busy state with the poll timeout instead of stalling, because the target may take longer
// than the timeout it reported and a stall would fail the host for a slow flash. A host that waits the reported timeout never sees
// the difference. A host that polls early gets the busy answer, not dfuERROR.
//
// wBlockNum is not checked. The stream offset orders the blocks, so a host that repeats a block writes it again at the next
// offset. dfu-util numbers the blocks of one transfer from 0 and does not repeat them.
//
// Memory regions are alternate settings (USBDfuBuilder::Region()). SET_INTERFACE selects one, and the transfer that starts after it
// goes to that region: Begin() receives the alternate. SET_INTERFACE is accepted only in dfuIDLE with no write or manifestation
// running, so a transfer keeps its region from start to end; a switch in the middle of a download would send the rest of the
// stream elsewhere. An alternate the descriptor does not list, or a switch at the wrong time, stalls and leaves the state alone.
// GET_INTERFACE answers the selection. A bus reset, deconfiguration or configuration returns to alternate 0.
//
// TransferSize is the size of the block buffer. It must be at least the wTransferSize of the descriptor, which OnConfigured checks.
template<std::size_t TransferSize>
class DfuFunction : public USBFunction {
public:
	// Called from Process(). willDetach is true when the device must detach and attach itself (bitWillDetach, after the
	// manifestation). It is false when a device that does not stay on the bus must restart after the host's reset. In both cases the
	// application restarts the device: a reset of the MCU, since USBDevice::Stop() and Start() present the same descriptors again.
	using DetachDelegate = Callback::Delegate<void(bool willDetach)>;


private:
	static_assert(TransferSize >= 1 && TransferSize <= DfuMaxTransferSize, "the DFU block buffer is 1 to 4096 bytes");


	IDfuTarget& target;
	DetachDelegate detachDelegate;
	USBDfuFunctional functional;
	USBDfuAlternates alternates;
	std::array<uint8, TransferSize> block{};
	DfuState state = DfuState::dfuIDLE;
	DfuStatus status = DfuStatus::OK;
	uint32 blockLength = 0;
	uint32 offset = 0;
	uint8 selectedAlternate = 0;
	bool writePending = false;
	bool manifestComplete = false;
	bool targetBusy = false;
	bool restartPending = false;
	std::array<uint8, 6> statusResponse{};
	std::array<uint8, 1> stateResponse{};


public:
	// The layout of the DFU mode interface (USBFunctionLayout from the fluent builder) and the target that holds the memory.
	DfuFunction(const USBFunctionLayout& layout, IDfuTarget& dfuTarget)
		: USBFunction(layout),
		  target(dfuTarget),
		  functional(ParseUSBDfuFunctional(layout.descriptors)),
		  alternates(ParseUSBDfuAlternates(layout.descriptors, layout.interfaces)) {
	}


	// Set before the device starts: the delegate is not guarded against a call that is already running.
	void SetDetachDelegate(DetachDelegate delegate) {
		detachDelegate = delegate;
	}


	DfuState GetState() const {
		return state;
	}


	DfuStatus GetStatus() const {
		return status;
	}


	// The layout must hold exactly one interface: the requests read interfaceNumbers[0] to name the alternate they start, so a
	// layout without one would read past the span.
	ResultStatus OnConfigured(USBDeviceContext&) override {
		if (!functional.found || functional.transferSize > TransferSize || interfaceNumbers.size() != 1) {
			return ResultStatus::invalidParameter;
		}

		ResetState();
		return ResultStatus::ok;
	}


	void OnDeconfigured() override {
		ResetState();
	}


	void OnBusReset() override {
		// A device that does not stay on the bus waits for the host's reset to restart. The reset is that signal.
		if (state == DfuState::dfuMANIFEST_WAIT_RESET && !WillDetach()) {
			restartPending = true;
		}

		ResetState();
	}


	// SET_INTERFACE selects a memory region. Only an alternate the descriptor lists is accepted, and only while the device is idle with
	// no write or manifestation running (see the comment at the top). Anything else returns notSupported, which the core stalls; the
	// state stays as it was, since SET_INTERFACE is a standard request and not a DFU one.
	ResultStatus OnSetInterface(uint8 interfaceNumber, uint8 alternate, USBDeviceContext&) override {
		if (interfaceNumber != interfaceNumbers[0] || !alternates.Has(alternate)) {
			return ResultStatus::notSupported;
		}

		if (state != DfuState::dfuIDLE || targetBusy) {
			return ResultStatus::notSupported;
		}

		selectedAlternate = alternate;
		return ResultStatus::ok;
	}


	// GET_INTERFACE answers the selected alternate. The function has one interface (OnConfigured checks it), so the number is not needed.
	uint8 GetInterface(uint8) const override {
		return selectedAlternate;
	}


	// Requests of the interface. Everything the table of appendix A does not allow stalls with errSTALLEDPKT.
	ResultStatus OnControlRequest(const USBSetupPacket& setup, USBDeviceContext& context) override {
		// The device after the manifestation cannot answer on the bus (bitManifestationTolerant clear). The specification has it do
		// nothing here. The stack cannot leave a control request unanswered, so it refuses the request: the core stalls it, and the
		// state does not change. The next reset is the one that matters: restartPending delivers it.
		if (state == DfuState::dfuMANIFEST_WAIT_RESET) {
			return ResultStatus::notSupported;
		}

		// A standard request to the interface (SET_FEATURE, for one) is not a DFU request: the core stalls it, the state stays.
		if (setup.type != USBRequestType::Class) {
			return ResultStatus::notSupported;
		}

		if (setup.request > static_cast<uint8>(DfuRequest::Abort)) {
			return Stall(DfuStatus::errSTALLEDPKT);
		}

		DfuRequest request = static_cast<DfuRequest>(setup.request);
		if (request == DfuRequest::Detach || setup.direction != DfuRequestDirection(request)) {
			return Stall(DfuStatus::errSTALLEDPKT);
		}

		if (request == DfuRequest::Dnload) {
			return HandleDownload(setup, context);
		}

		if (request == DfuRequest::Upload) {
			return HandleUpload(setup, context);
		}

		if (request == DfuRequest::GetStatus) {
			return HandleGetStatus(context);
		}

		if (request == DfuRequest::GetState) {
			return HandleGetState(context);
		}

		if (setup.length != 0) {
			return Stall(DfuStatus::errSTALLEDPKT);
		}

		if (request == DfuRequest::ClrStatus) {
			return HandleClearStatus();
		}

		return HandleAbort();
	}


	// The OUT data stage of a DFU_DNLOAD block that OnControlRequest() received. The block is pending until Process() writes it.
	ResultStatus OnControlDataReceived(const USBSetupPacket& setup, std::span<const uint8> data, USBDeviceContext&) override {
		if (setup.request != static_cast<uint8>(DfuRequest::Dnload)) {
			return ResultStatus::notSupported;
		}

		blockLength = static_cast<uint32>(data.size());
		writePending = true;
		state = DfuState::dfuDNLOAD_SYNC;
		return ResultStatus::ok;
	}


	// Runs the target work of the state the interrupt left the device in. The state, the block's offset and length, and the busy flag
	// are read and set in one short critical section; the target runs outside it.
	void Process(USBDeviceContext&) override {
		DfuState current = DfuState::dfuIDLE;
		uint32 start = 0;
		uint32 length = 0;
		bool restart = false;

		System::CriticalSection(true);
		current = state;
		start = offset;
		length = blockLength;
		restart = restartPending;
		restartPending = false;
		if (current == DfuState::dfuDNBUSY || current == DfuState::dfuMANIFEST) {
			targetBusy = true;
		}

		System::CriticalSection(false);

		if (current == DfuState::dfuDNBUSY) {
			RunWrite(start, length);
		} else if (current == DfuState::dfuMANIFEST) {
			RunManifest();
		}

		if (restart && detachDelegate) {
			detachDelegate(false);
		}
	}


private:
	// Moves to dfuERROR with the code, unless the device is already there: an error keeps the first code until DFU_CLRSTATUS. The
	// stall itself is the return value, so the core stalls the control pipe.
	ResultStatus Stall(DfuStatus code) {
		if (state != DfuState::dfuERROR) {
			state = DfuState::dfuERROR;
			status = code;
		}

		return ResultStatus::notSupported;
	}


	bool CanDnload() const {
		return (functional.attributes & DfuAttributeCanDnload) != 0;
	}


	bool CanUpload() const {
		return (functional.attributes & DfuAttributeCanUpload) != 0;
	}


	bool IsManifestationTolerant() const {
		return (functional.attributes & DfuAttributeManifestationTolerant) != 0;
	}


	bool WillDetach() const {
		return (functional.attributes & DfuAttributeWillDetach) != 0;
	}


	// Clears the transfer and returns to dfuIDLE with status OK. Called by a bus reset, deconfiguration and configuration. The busy
	// flag is not cleared here: a write or a manifestation that is still running keeps the target until it returns.
	void ResetState() {
		state = DfuState::dfuIDLE;
		status = DfuStatus::OK;
		blockLength = 0;
		offset = 0;
		selectedAlternate = 0;
		writePending = false;
		manifestComplete = false;
	}


	// DFU_DNLOAD. wLength 0 is the end of the download: dfuDNLOAD-IDLE becomes dfuMANIFEST-SYNC when the target has everything,
	// and dfuIDLE stalls (a zero-length download is not useful before any block). wLength above the transfer size stalls, and so
	// does any download while the target is busy.
	ResultStatus HandleDownload(const USBSetupPacket& setup, USBDeviceContext& context) {
		if (!CanDnload() || targetBusy) {
			return Stall(DfuStatus::errSTALLEDPKT);
		}

		if (setup.length == 0) {
			if (state != DfuState::dfuDNLOAD_IDLE) {
				return Stall(DfuStatus::errSTALLEDPKT);
			}

			if (!target.IsComplete()) {
				return Stall(DfuStatus::errNOTDONE);
			}

			state = DfuState::dfuMANIFEST_SYNC;
			manifestComplete = false;
			return ResultStatus::ok;
		}

		if (setup.length > functional.transferSize) {
			return Stall(DfuStatus::errSTALLEDPKT);
		}

		if (state == DfuState::dfuIDLE) {
			DfuStatus begun = target.Begin(selectedAlternate);
			if (begun != DfuStatus::OK) {
				return Stall(begun);
			}

			offset = 0;
		} else if (state != DfuState::dfuDNLOAD_IDLE) {
			return Stall(DfuStatus::errSTALLEDPKT);
		}

		if (context.ControlReceiveData(std::span<uint8>(block).first(setup.length)) != ResultStatus::ok) {
			return Stall(DfuStatus::errUNKNOWN);
		}

		return ResultStatus::ok;
	}


	// DFU_UPLOAD. The target's read fills the block; a block shorter than wLength ends the upload and leaves dfuIDLE (appendix A.2.2:
	// the short frame is the one shorter than the request). A block that fills wLength, even one that is wTransferSize long, keeps
	// the upload going. A read error stalls with errUNKNOWN, since the target gives no finer code for a read. A busy target stalls
	// the upload.
	ResultStatus HandleUpload(const USBSetupPacket& setup, USBDeviceContext& context) {
		if (!CanUpload() || setup.length == 0 || targetBusy) {
			return Stall(DfuStatus::errSTALLEDPKT);
		}

		if (state == DfuState::dfuIDLE) {
			DfuStatus begun = target.Begin(selectedAlternate);
			if (begun != DfuStatus::OK) {
				return Stall(begun);
			}

			offset = 0;
		} else if (state != DfuState::dfuUPLOAD_IDLE) {
			return Stall(DfuStatus::errSTALLEDPKT);
		}

		uint32 requested = setup.length < functional.transferSize ? setup.length : functional.transferSize;
		Result<uint32> read = target.Read(offset, std::span<uint8>(block).first(requested));
		if (!read.IsOk()) {
			return Stall(DfuStatus::errUNKNOWN);
		}

		uint32 count = read.Value() < requested ? read.Value() : requested;
		if (context.ControlSendData(std::span<const uint8>(block).first(count)) != ResultStatus::ok) {
			return Stall(DfuStatus::errUNKNOWN);
		}

		offset += count;
		state = count < setup.length ? DfuState::dfuIDLE : DfuState::dfuUPLOAD_IDLE;
		return ResultStatus::ok;
	}


	// DFU_GETSTATUS. The answer carries the state the device enters next, so the transitions that GETSTATUS causes happen first:
	// dfuDNLOAD-SYNC to dfuDNBUSY (block still to write) or dfuDNLOAD-IDLE (written), and dfuMANIFEST-SYNC to dfuMANIFEST (first
	// status request) or dfuIDLE (manifestation done, device can still answer).
	ResultStatus HandleGetStatus(USBDeviceContext& context) {
		DfuStatus reported = status;
		uint32 pollTimeoutMs = 0;

		if (state == DfuState::dfuDNLOAD_SYNC) {
			if (writePending) {
				state = DfuState::dfuDNBUSY;
				pollTimeoutMs = target.GetPollTimeoutMs(DfuOperation::Write);
			} else {
				state = DfuState::dfuDNLOAD_IDLE;
			}
		} else if (state == DfuState::dfuDNBUSY) {
			pollTimeoutMs = target.GetPollTimeoutMs(DfuOperation::Write);
		} else if (state == DfuState::dfuMANIFEST_SYNC) {
			if (manifestComplete) {
				state = DfuState::dfuIDLE;
				manifestComplete = false;
			} else {
				state = DfuState::dfuMANIFEST;
				pollTimeoutMs = target.GetPollTimeoutMs(DfuOperation::Manifest);
			}
		} else if (state == DfuState::dfuMANIFEST) {
			pollTimeoutMs = target.GetPollTimeoutMs(DfuOperation::Manifest);
		}

		statusResponse = MakeUSBDfuStatusResponse(reported, pollTimeoutMs, state);
		if (context.ControlSendData(statusResponse) != ResultStatus::ok) {
			return Stall(DfuStatus::errUNKNOWN);
		}

		return ResultStatus::ok;
	}


	// DFU_GETSTATE. Reports the state without changing it. Not answered while a write or a manifestation runs (appendix A.2.5 and
	// A.2.7 stall every class request there).
	ResultStatus HandleGetState(USBDeviceContext& context) {
		if (state == DfuState::dfuDNBUSY || state == DfuState::dfuMANIFEST) {
			return Stall(DfuStatus::errSTALLEDPKT);
		}

		stateResponse[0] = static_cast<uint8>(state);
		if (context.ControlSendData(stateResponse) != ResultStatus::ok) {
			return Stall(DfuStatus::errUNKNOWN);
		}

		return ResultStatus::ok;
	}


	// DFU_CLRSTATUS. Only in dfuERROR: the status goes to OK and the state to dfuIDLE.
	ResultStatus HandleClearStatus() {
		if (state != DfuState::dfuERROR) {
			return Stall(DfuStatus::errSTALLEDPKT);
		}

		state = DfuState::dfuIDLE;
		status = DfuStatus::OK;
		return ResultStatus::ok;
	}


	// DFU_ABORT. Ends a transfer from dfuIDLE, dfuDNLOAD-IDLE or dfuUPLOAD-IDLE and returns to dfuIDLE with status OK.
	ResultStatus HandleAbort() {
		if (state == DfuState::dfuIDLE || state == DfuState::dfuDNLOAD_IDLE || state == DfuState::dfuUPLOAD_IDLE) {
			state = DfuState::dfuIDLE;
			status = DfuStatus::OK;
			offset = 0;
			return ResultStatus::ok;
		}

		return Stall(DfuStatus::errSTALLEDPKT);
	}


	// Writes the pending block at start. The result is committed only while the device is still in dfuDNBUSY: a bus reset, a
	// deconfiguration or a stall that came in the meantime keeps its own state.
	void RunWrite(uint32 start, uint32 length) {
		DfuStatus result = target.Write(start, std::span<const uint8>(block).first(length));

		System::CriticalSection(true);
		targetBusy = false;
		writePending = false;
		if (state == DfuState::dfuDNBUSY) {
			if (result == DfuStatus::OK) {
				offset += length;
				state = DfuState::dfuDNLOAD_SYNC;
			} else {
				state = DfuState::dfuERROR;
				status = result;
			}
		}

		System::CriticalSection(false);
	}


	// Manifests the image. A device that stays on the bus goes to dfuMANIFEST-SYNC; one that does not goes to dfuMANIFEST-WAIT-RESET,
	// and asks the application to detach when bitWillDetach is set (the delegate runs outside the critical section). The result is
	// committed only while the device is still in dfuMANIFEST, as RunWrite() does.
	void RunManifest() {
		DfuStatus result = target.Manifest();
		bool detach = false;

		System::CriticalSection(true);
		targetBusy = false;
		if (state == DfuState::dfuMANIFEST) {
			if (result == DfuStatus::OK) {
				manifestComplete = true;
				if (IsManifestationTolerant()) {
					state = DfuState::dfuMANIFEST_SYNC;
				} else {
					state = DfuState::dfuMANIFEST_WAIT_RESET;
					detach = WillDetach();
				}
			} else {
				state = DfuState::dfuERROR;
				status = result;
			}
		}

		System::CriticalSection(false);

		if (detach && detachDelegate) {
			detachDelegate(true);
		}
	}
};
