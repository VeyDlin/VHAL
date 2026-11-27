#include <VHAL.h>
#include "DfuTestDevices.h"
#include "SimAssert.h"
#include "SimHost.h"
#include <Drivers/Interface/USB/Class/DFU/DfuFunction.h>
#include <Drivers/Interface/USB/Class/DFU/USBDfuProtocol.h>
#include <cstdint>
#include <cstdio>
#include <vector>


// The DFU mode state machine, transition by transition, over a fake target. The states and status codes are the ones of DFU 1.1
// appendix A: a request that the table does not allow stalls and moves the device to dfuERROR with errSTALLEDPKT (or with the
// status the failure had), and DFU_CLRSTATUS is the only way out of dfuERROR. Writes and manifestation run in Process(), so each
// test calls Process() where the device would be between two requests.


using namespace DfuTest;


namespace {


constexpr uint8 RequestDetach = static_cast<uint8>(DfuRequest::Detach);
constexpr uint8 RequestDnload = static_cast<uint8>(DfuRequest::Dnload);
constexpr uint8 RequestUpload = static_cast<uint8>(DfuRequest::Upload);
constexpr uint8 RequestGetStatus = static_cast<uint8>(DfuRequest::GetStatus);
constexpr uint8 RequestClrStatus = static_cast<uint8>(DfuRequest::ClrStatus);
constexpr uint8 RequestGetState = static_cast<uint8>(DfuRequest::GetState);
constexpr uint8 RequestAbort = static_cast<uint8>(DfuRequest::Abort);


uint32 detachCalls = 0;
bool lastWillDetach = false;


void OnDetach(bool willDetach) {
	detachCalls++;
	lastWillDetach = willDetach;
}


// A device context for the calls the function makes without a bus: the control answers can be made to fail, as a core whose send
// fails does. The other calls are not used by DFU and answer notSupported.
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


// count bytes of a sequence that starts at start: start, start + 1, ...
std::vector<uint8> Pattern(std::size_t count, uint8 start) {
	std::vector<uint8> bytes(count);
	for (std::size_t index = 0; index < count; index++) {
		bytes[index] = static_cast<uint8>(start + index);
	}

	return bytes;
}


// Sends one download block and checks that the request was accepted.
void Download(SimUSBDeviceAdapter& sim, const std::vector<uint8>& block) {
	SIM_EXPECT_EQ(SendOut(sim, RequestDnload, 0, block), ResultStatus::ok);
}


// Sends a block and runs the write the way the host and the device do: GETSTATUS starts it, Process() writes, and the next
// GETSTATUS reports the block complete.
template<typename UsbType>
void DownloadAndWrite(ModeDevice<UsbType>& fixture, const std::vector<uint8>& block) {
	Download(fixture.sim, block);
	StatusAnswer busy = ReadStatus(fixture.sim);
	SIM_EXPECT_EQ(busy.state, DfuState::dfuDNBUSY);
	fixture.device.Process();
	StatusAnswer done = ReadStatus(fixture.sim);
	SIM_EXPECT_EQ(done.state, DfuState::dfuDNLOAD_IDLE);
	SIM_EXPECT_EQ(done.status, DfuStatus::OK);
}


// Reads one upload block of up to length bytes. The answer is the data the device sent.
std::vector<uint8> Upload(SimUSBDeviceAdapter& sim, uint16 length, ResultStatus& result) {
	Answer answer = GetIn(sim, RequestUpload, 0, length);
	result = answer.status;
	return answer.data;
}


void CheckDownloadHappyPath() {
	ModeDevice<ModeUsb> fixture;
	fixture.Configure();
	SIM_EXPECT_EQ(ReadState(fixture.sim), DfuState::dfuIDLE);

	// A full block: the write waits for GETSTATUS, which answers dfuDNBUSY with the target's write poll timeout.
	std::vector<uint8> first = Pattern(64, 0x10);
	Download(fixture.sim, first);
	StatusAnswer busy = ReadStatus(fixture.sim);
	SIM_EXPECT(busy.valid);
	SIM_EXPECT_EQ(busy.state, DfuState::dfuDNBUSY);
	SIM_EXPECT_EQ(busy.status, DfuStatus::OK);
	SIM_EXPECT_EQ(busy.pollTimeoutMs, 15);
	SIM_EXPECT(fixture.target.writes.empty());

	// Process() writes the block at offset 0. The next GETSTATUS reports dfuDNLOAD-IDLE.
	fixture.device.Process();
	SIM_EXPECT_EQ(fixture.target.writes.size(), 1);
	SIM_EXPECT_EQ(fixture.target.writes[0].offset, 0);
	SIM_EXPECT(fixture.target.writes[0].data == first);
	StatusAnswer idle = ReadStatus(fixture.sim);
	SIM_EXPECT_EQ(idle.state, DfuState::dfuDNLOAD_IDLE);
	SIM_EXPECT_EQ(idle.pollTimeoutMs, 0);

	// A short last block: the offset carries on from the first block, and the length is the block's own.
	std::vector<uint8> last = Pattern(10, 0x80);
	DownloadAndWrite(fixture, last);
	SIM_EXPECT_EQ(fixture.target.writes.size(), 2);
	SIM_EXPECT_EQ(fixture.target.writes[1].offset, 64);
	SIM_EXPECT(fixture.target.writes[1].data == last);

	// wLength 0 ends the download: dfuMANIFEST-SYNC. The first GETSTATUS starts the manifestation (dfuMANIFEST, poll 40 ms).
	SIM_EXPECT_EQ(SendNoData(fixture.sim, RequestDnload, 0), ResultStatus::ok);
	StatusAnswer manifest = ReadStatus(fixture.sim);
	SIM_EXPECT_EQ(manifest.state, DfuState::dfuMANIFEST);
	SIM_EXPECT_EQ(manifest.pollTimeoutMs, 40);
	SIM_EXPECT_EQ(fixture.target.manifestCount, 0);
	fixture.device.Process();
	SIM_EXPECT_EQ(fixture.target.manifestCount, 1);

	// A manifestation-tolerant device stays on the bus: the status after the manifestation is dfuIDLE with OK.
	StatusAnswer done = ReadStatus(fixture.sim);
	SIM_EXPECT_EQ(done.state, DfuState::dfuIDLE);
	SIM_EXPECT_EQ(done.status, DfuStatus::OK);
	SIM_EXPECT_EQ(ReadState(fixture.sim), DfuState::dfuIDLE);
}


void CheckBusyPollReportsAndClamp() {
	ModeDevice<ModeUsb> fixture;
	fixture.Configure();
	fixture.target.writePollMs = 0x01000000;
	Download(fixture.sim, Pattern(64, 0));
	StatusAnswer busy = ReadStatus(fixture.sim);
	SIM_EXPECT_EQ(busy.state, DfuState::dfuDNBUSY);
	SIM_EXPECT_EQ(busy.pollTimeoutMs, 0xFFFFFF);

	// A status request before the write is done is answered busy, not stalled: the target may take longer than its estimate.
	StatusAnswer early = ReadStatus(fixture.sim);
	SIM_EXPECT(early.valid);
	SIM_EXPECT_EQ(early.state, DfuState::dfuDNBUSY);
	SIM_EXPECT_EQ(early.status, DfuStatus::OK);

	// DFU_GETSTATE is not answered while the write runs: it stalls and the device is in dfuERROR.
	SIM_EXPECT_EQ(GetIn(fixture.sim, RequestGetState, 0, 1).status, ResultStatus::accessError);
	StatusAnswer stalled = ReadStatus(fixture.sim);
	SIM_EXPECT_EQ(stalled.state, DfuState::dfuERROR);
	SIM_EXPECT_EQ(stalled.status, DfuStatus::errSTALLEDPKT);
}


void CheckWriteFailure(DfuStatus code) {
	ModeDevice<ModeUsb> fixture;
	fixture.Configure();
	fixture.target.writeResult = code;
	Download(fixture.sim, Pattern(64, 0));
	ReadStatus(fixture.sim);
	fixture.device.Process();
	StatusAnswer failed = ReadStatus(fixture.sim);
	SIM_EXPECT_EQ(failed.state, DfuState::dfuERROR);
	SIM_EXPECT_EQ(failed.status, code);
}


void CheckWriteErrorIsStickyUntilClear() {
	ModeDevice<ModeUsb> fixture;
	fixture.Configure();
	fixture.target.writeResult = DfuStatus::errERASE;
	Download(fixture.sim, Pattern(64, 0));
	ReadStatus(fixture.sim);
	fixture.device.Process();

	// Every request but GETSTATUS, GETSTATE and CLRSTATUS stalls in dfuERROR. The first error code stays.
	std::vector<uint8> more = Pattern(64, 0);
	SIM_EXPECT_EQ(SendOut(fixture.sim, RequestDnload, 0, more), ResultStatus::accessError);
	SIM_EXPECT_EQ(SendNoData(fixture.sim, RequestAbort, 0), ResultStatus::accessError);
	StatusAnswer stillError = ReadStatus(fixture.sim);
	SIM_EXPECT_EQ(stillError.state, DfuState::dfuERROR);
	SIM_EXPECT_EQ(stillError.status, DfuStatus::errERASE);

	// DFU_CLRSTATUS returns to dfuIDLE with OK, and only then does a download start again.
	SIM_EXPECT_EQ(SendNoData(fixture.sim, RequestClrStatus, 0), ResultStatus::ok);
	StatusAnswer cleared = ReadStatus(fixture.sim);
	SIM_EXPECT_EQ(cleared.state, DfuState::dfuIDLE);
	SIM_EXPECT_EQ(cleared.status, DfuStatus::OK);
}


void CheckBeginFailureIsTargetError() {
	ModeDevice<ModeUsb> fixture;
	fixture.Configure();
	fixture.target.beginResult = DfuStatus::errTARGET;
	SIM_EXPECT_EQ(SendOut(fixture.sim, RequestDnload, 0, Pattern(64, 0)), ResultStatus::accessError);
	SIM_EXPECT_EQ(fixture.target.beginCount, 1);
	StatusAnswer status = ReadStatus(fixture.sim);
	SIM_EXPECT_EQ(status.state, DfuState::dfuERROR);
	SIM_EXPECT_EQ(status.status, DfuStatus::errTARGET);
}


void CheckZeroLengthDownloadInIdleStalls() {
	ModeDevice<ModeUsb> fixture;
	fixture.Configure();
	SIM_EXPECT_EQ(SendNoData(fixture.sim, RequestDnload, 0), ResultStatus::accessError);
	StatusAnswer status = ReadStatus(fixture.sim);
	SIM_EXPECT_EQ(status.state, DfuState::dfuERROR);
	SIM_EXPECT_EQ(status.status, DfuStatus::errSTALLEDPKT);
}


void CheckZeroLengthNotDoneIsErrNotDone() {
	ModeDevice<ModeUsb> fixture;
	fixture.Configure();
	fixture.target.complete = false;
	DownloadAndWrite(fixture, Pattern(64, 0));
	SIM_EXPECT_EQ(SendNoData(fixture.sim, RequestDnload, 0), ResultStatus::accessError);
	StatusAnswer status = ReadStatus(fixture.sim);
	SIM_EXPECT_EQ(status.state, DfuState::dfuERROR);
	SIM_EXPECT_EQ(status.status, DfuStatus::errNOTDONE);
	SIM_EXPECT_EQ(fixture.target.manifestCount, 0);
}


void CheckOversizedBlockStalls() {
	ModeDevice<ModeUsb> fixture;
	fixture.Configure();
	SIM_EXPECT_EQ(SendOut(fixture.sim, RequestDnload, 0, Pattern(65, 0)), ResultStatus::accessError);
	StatusAnswer status = ReadStatus(fixture.sim);
	SIM_EXPECT_EQ(status.state, DfuState::dfuERROR);
	SIM_EXPECT_EQ(status.status, DfuStatus::errSTALLEDPKT);
	SIM_EXPECT(fixture.target.writes.empty());
}


void CheckDownloadInSyncStalls() {
	ModeDevice<ModeUsb> fixture;
	fixture.Configure();
	Download(fixture.sim, Pattern(64, 0));

	// dfuDNLOAD-SYNC takes only GETSTATUS and GETSTATE; a second block is a stall.
	SIM_EXPECT_EQ(SendOut(fixture.sim, RequestDnload, 0, Pattern(64, 0)), ResultStatus::accessError);
	StatusAnswer status = ReadStatus(fixture.sim);
	SIM_EXPECT_EQ(status.state, DfuState::dfuERROR);
	SIM_EXPECT_EQ(status.status, DfuStatus::errSTALLEDPKT);
}


void CheckAbortRules() {
	ModeDevice<ModeUsb> idle;
	idle.Configure();
	SIM_EXPECT_EQ(SendNoData(idle.sim, RequestAbort, 0), ResultStatus::ok);
	SIM_EXPECT_EQ(ReadState(idle.sim), DfuState::dfuIDLE);

	ModeDevice<ModeUsb> downloading;
	downloading.Configure();
	DownloadAndWrite(downloading, Pattern(64, 0));
	SIM_EXPECT_EQ(SendNoData(downloading.sim, RequestAbort, 0), ResultStatus::ok);
	SIM_EXPECT_EQ(ReadState(downloading.sim), DfuState::dfuIDLE);

	// ABORT in dfuDNLOAD-SYNC is one of the requests the state does not take.
	ModeDevice<ModeUsb> syncing;
	syncing.Configure();
	Download(syncing.sim, Pattern(64, 0));
	SIM_EXPECT_EQ(SendNoData(syncing.sim, RequestAbort, 0), ResultStatus::accessError);
	StatusAnswer status = ReadStatus(syncing.sim);
	SIM_EXPECT_EQ(status.state, DfuState::dfuERROR);
	SIM_EXPECT_EQ(status.status, DfuStatus::errSTALLEDPKT);
}


void CheckClearStatusInIdleStalls() {
	ModeDevice<ModeUsb> fixture;
	fixture.Configure();
	SIM_EXPECT_EQ(SendNoData(fixture.sim, RequestClrStatus, 0), ResultStatus::accessError);
	StatusAnswer status = ReadStatus(fixture.sim);
	SIM_EXPECT_EQ(status.state, DfuState::dfuERROR);
	SIM_EXPECT_EQ(status.status, DfuStatus::errSTALLEDPKT);
}


void CheckUnknownRequestsStall() {
	ModeDevice<ModeUsb> unknown;
	unknown.Configure();
	SIM_EXPECT_EQ(SendNoData(unknown.sim, 9, 0), ResultStatus::accessError);
	StatusAnswer unknownStatus = ReadStatus(unknown.sim);
	SIM_EXPECT_EQ(unknownStatus.state, DfuState::dfuERROR);
	SIM_EXPECT_EQ(unknownStatus.status, DfuStatus::errSTALLEDPKT);

	// DFU_DETACH is a run-time request: a mode device stalls it.
	ModeDevice<ModeUsb> detach;
	detach.Configure();
	SIM_EXPECT_EQ(SendNoData(detach.sim, RequestDetach, 100), ResultStatus::accessError);
	SIM_EXPECT_EQ(ReadStatus(detach.sim).state, DfuState::dfuERROR);

	// DFU_GETSTATUS sent as an OUT request is the wrong direction: stall.
	ModeDevice<ModeUsb> direction;
	direction.Configure();
	SIM_EXPECT_EQ(SendOut(direction.sim, RequestGetStatus, 0, std::span<const uint8>()), ResultStatus::accessError);
	SIM_EXPECT_EQ(ReadStatus(direction.sim).state, DfuState::dfuERROR);
}


void CheckUploadOnlyRefusesDownload() {
	ModeDevice<UploadOnlyUsb> fixture;
	fixture.Configure();
	SIM_EXPECT_EQ(SendOut(fixture.sim, RequestDnload, 0, Pattern(64, 0)), ResultStatus::accessError);
	StatusAnswer status = ReadStatus(fixture.sim);
	SIM_EXPECT_EQ(status.state, DfuState::dfuERROR);
	SIM_EXPECT_EQ(status.status, DfuStatus::errSTALLEDPKT);
}


void CheckDownloadOnlyRefusesUpload() {
	ModeDevice<WaitResetUsb> fixture;
	fixture.Configure();
	ResultStatus result = ResultStatus::ok;
	Upload(fixture.sim, 64, result);
	SIM_EXPECT_EQ(result, ResultStatus::accessError);
	StatusAnswer status = ReadStatus(fixture.sim);
	SIM_EXPECT_EQ(status.state, DfuState::dfuERROR);
	SIM_EXPECT_EQ(status.status, DfuStatus::errSTALLEDPKT);
}


void CheckUploadEndsOnShortBlock() {
	ModeDevice<ModeUsb> fixture;
	fixture.Configure();
	fixture.target.image = Pattern(150, 0x00);

	ResultStatus result = ResultStatus::ok;
	std::vector<uint8> first = Upload(fixture.sim, 64, result);
	SIM_EXPECT_EQ(result, ResultStatus::ok);
	SIM_EXPECT(first == Pattern(64, 0x00));
	SIM_EXPECT_EQ(ReadState(fixture.sim), DfuState::dfuUPLOAD_IDLE);

	std::vector<uint8> second = Upload(fixture.sim, 64, result);
	SIM_EXPECT(second == Pattern(64, 0x40));
	SIM_EXPECT_EQ(ReadState(fixture.sim), DfuState::dfuUPLOAD_IDLE);

	// 22 bytes are left: a short block is the end of the upload, and the device is in dfuIDLE.
	std::vector<uint8> last = Upload(fixture.sim, 64, result);
	SIM_EXPECT_EQ(last.size(), 22);
	SIM_EXPECT(last == Pattern(22, 0x80));
	SIM_EXPECT_EQ(ReadState(fixture.sim), DfuState::dfuIDLE);
}


void CheckUploadExactMultipleEndsWithEmptyBlock() {
	ModeDevice<ModeUsb> fixture;
	fixture.Configure();
	fixture.target.image = Pattern(128, 0x00);

	ResultStatus result = ResultStatus::ok;
	Upload(fixture.sim, 64, result);
	Upload(fixture.sim, 64, result);
	SIM_EXPECT_EQ(ReadState(fixture.sim), DfuState::dfuUPLOAD_IDLE);

	// The block after the last full one is empty: the zero-length answer is the end of the upload.
	std::vector<uint8> none = Upload(fixture.sim, 64, result);
	SIM_EXPECT_EQ(result, ResultStatus::ok);
	SIM_EXPECT_EQ(none.size(), 0);
	SIM_EXPECT_EQ(ReadState(fixture.sim), DfuState::dfuIDLE);
}


void CheckUploadAbort() {
	ModeDevice<ModeUsb> fixture;
	fixture.Configure();
	fixture.target.image = Pattern(150, 0x00);
	ResultStatus result = ResultStatus::ok;
	Upload(fixture.sim, 64, result);
	SIM_EXPECT_EQ(ReadState(fixture.sim), DfuState::dfuUPLOAD_IDLE);
	SIM_EXPECT_EQ(SendNoData(fixture.sim, RequestAbort, 0), ResultStatus::ok);
	SIM_EXPECT_EQ(ReadState(fixture.sim), DfuState::dfuIDLE);
}


void CheckUploadReadErrorStalls() {
	ModeDevice<ModeUsb> fixture;
	fixture.Configure();
	fixture.target.readFails = true;
	ResultStatus result = ResultStatus::ok;
	Upload(fixture.sim, 64, result);
	SIM_EXPECT_EQ(result, ResultStatus::accessError);
	StatusAnswer status = ReadStatus(fixture.sim);
	SIM_EXPECT_EQ(status.state, DfuState::dfuERROR);
	SIM_EXPECT_EQ(status.status, DfuStatus::errUNKNOWN);
}


void CheckManifestWithoutToleranceWaitsForReset() {
	ModeDevice<WaitResetUsb> fixture;
	fixture.Configure();
	fixture.function.SetDetachDelegate(DfuFunction<64>::DetachDelegate::Bind<&OnDetach>());
	detachCalls = 0;

	DownloadAndWrite(fixture, Pattern(64, 0));
	SIM_EXPECT_EQ(SendNoData(fixture.sim, RequestDnload, 0), ResultStatus::ok);
	ReadStatus(fixture.sim);
	fixture.device.Process();
	SIM_EXPECT_EQ(fixture.target.manifestCount, 1);
	SIM_EXPECT_EQ(fixture.function.GetState(), DfuState::dfuMANIFEST_WAIT_RESET);

	// Without bitWillDetach the host must reset the device: nothing is asked of the application yet, and the core stalls the requests.
	SIM_EXPECT_EQ(detachCalls, 0);
	SIM_EXPECT_EQ(GetIn(fixture.sim, RequestGetStatus, 0, 6).status, ResultStatus::accessError);
	SIM_EXPECT_EQ(fixture.function.GetState(), DfuState::dfuMANIFEST_WAIT_RESET);
}


void CheckManifestWithWillDetachAsksForDetach() {
	ModeDevice<DetachUsb> fixture;
	fixture.Configure();
	fixture.function.SetDetachDelegate(DfuFunction<64>::DetachDelegate::Bind<&OnDetach>());
	detachCalls = 0;

	DownloadAndWrite(fixture, Pattern(64, 0));
	SIM_EXPECT_EQ(SendNoData(fixture.sim, RequestDnload, 0), ResultStatus::ok);
	ReadStatus(fixture.sim);
	fixture.device.Process();
	SIM_EXPECT_EQ(fixture.function.GetState(), DfuState::dfuMANIFEST_WAIT_RESET);
	SIM_EXPECT_EQ(detachCalls, 1);
	SIM_EXPECT(lastWillDetach);
}


void CheckManifestErrorIsReported() {
	ModeDevice<ModeUsb> fixture;
	fixture.Configure();
	fixture.target.manifestResult = DfuStatus::errVENDOR;
	DownloadAndWrite(fixture, Pattern(64, 0));
	SIM_EXPECT_EQ(SendNoData(fixture.sim, RequestDnload, 0), ResultStatus::ok);
	ReadStatus(fixture.sim);
	fixture.device.Process();
	StatusAnswer status = ReadStatus(fixture.sim);
	SIM_EXPECT_EQ(status.state, DfuState::dfuERROR);
	SIM_EXPECT_EQ(status.status, DfuStatus::errVENDOR);
}


void CheckBusResetDuringWriteDiscardsIt() {
	ModeDevice<ModeUsb> fixture;
	fixture.Configure();
	fixture.target.resetOnWrite = &fixture.sim;
	Download(fixture.sim, Pattern(64, 0));
	ReadStatus(fixture.sim);

	// The reset comes from inside Write(): the write finishes after the device left the transfer, so it changes nothing.
	fixture.device.Process();
	fixture.target.resetOnWrite = nullptr;
	SIM_EXPECT_EQ(fixture.target.writes.size(), 1);

	// Read before the host enumerates again: a re-enumeration would reset the state on its own and hide a late commit.
	SIM_EXPECT_EQ(fixture.function.GetState(), DfuState::dfuIDLE);
	SIM_EXPECT_EQ(fixture.function.GetStatus(), DfuStatus::OK);
	Enumerate(fixture.sim);
	SIM_EXPECT_EQ(ReadState(fixture.sim), DfuState::dfuIDLE);
}


void CheckStallDuringWriteKeepsError() {
	ModeDevice<ModeUsb> fixture;
	fixture.Configure();
	Download(fixture.sim, Pattern(64, 0));
	SIM_EXPECT_EQ(ReadStatus(fixture.sim).state, DfuState::dfuDNBUSY);

	// GETSTATE is not answered while the write runs: the stall moves the device to dfuERROR.
	SIM_EXPECT_EQ(GetIn(fixture.sim, RequestGetState, 0, 1).status, ResultStatus::accessError);

	// The write finishes after the stall. It must not move the device on to dfuDNLOAD-IDLE: the error stays.
	fixture.device.Process();
	StatusAnswer status = ReadStatus(fixture.sim);
	SIM_EXPECT(status.valid);
	SIM_EXPECT_EQ(status.state, DfuState::dfuERROR);
	SIM_EXPECT_EQ(status.status, DfuStatus::errSTALLEDPKT);
}


void CheckStandardRequestKeepsState() {
	ModeDevice<ModeUsb> fixture;
	fixture.Configure();

	// SET_FEATURE to the interface is a standard request: the core stalls it, and the DFU state does not change.
	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x01, 3, 0x0000, 0x0000, 0)), ResultStatus::accessError);
	SIM_EXPECT_EQ(ReadState(fixture.sim), DfuState::dfuIDLE);
	StatusAnswer status = ReadStatus(fixture.sim);
	SIM_EXPECT(status.valid);
	SIM_EXPECT_EQ(status.status, DfuStatus::OK);
}


void CheckReconfigureDuringWriteDiscardsIt() {
	ModeDevice<ModeUsb> fixture;
	fixture.Configure();

	// The host deconfigures and configures the device while the write runs. The result belongs to the old transfer and is dropped.
	fixture.target.duringWrite = [&fixture]() {
		SetConfiguration(fixture.sim, 0);
		SetConfiguration(fixture.sim, 1);
	};
	Download(fixture.sim, Pattern(64, 0));
	ReadStatus(fixture.sim);
	fixture.device.Process();
	fixture.target.duringWrite = nullptr;

	SIM_EXPECT_EQ(fixture.target.writes.size(), 1);
	SIM_EXPECT_EQ(fixture.function.GetState(), DfuState::dfuIDLE);
	SIM_EXPECT_EQ(fixture.function.GetStatus(), DfuStatus::OK);
}


void CheckBusResetDuringManifestDiscardsIt() {
	ModeDevice<ModeUsb> fixture;
	fixture.Configure();
	fixture.target.duringManifest = [&fixture]() {
		fixture.sim.InjectBusReset();
	};

	DownloadAndWrite(fixture, Pattern(64, 0));
	SIM_EXPECT_EQ(SendNoData(fixture.sim, RequestDnload, 0), ResultStatus::ok);
	ReadStatus(fixture.sim);
	fixture.device.Process();
	fixture.target.duringManifest = nullptr;

	// The manifestation finished after the reset: its result must not move the device on from dfuIDLE.
	SIM_EXPECT_EQ(fixture.target.manifestCount, 1);
	SIM_EXPECT_EQ(fixture.function.GetState(), DfuState::dfuIDLE);
	SIM_EXPECT_EQ(fixture.function.GetStatus(), DfuStatus::OK);
}


void CheckDownloadDuringWriteStalls() {
	ModeDevice<ModeUsb> fixture;
	fixture.Configure();
	std::vector<uint8> block = Pattern(64, 0x40);
	Download(fixture.sim, Pattern(64, 0));
	SIM_EXPECT_EQ(ReadStatus(fixture.sim).state, DfuState::dfuDNBUSY);

	// The host resets and starts a new download while the old write is still in the target. The target is in use, so the download
	// stalls: Begin() is not called again, and the old write does not commit.
	ResultStatus during = ResultStatus::ok;
	fixture.target.duringWrite = [&fixture, &block, &during]() {
		Enumerate(fixture.sim);
		during = SendOut(fixture.sim, RequestDnload, 0, block);
	};
	fixture.device.Process();
	fixture.target.duringWrite = nullptr;

	SIM_EXPECT_EQ(during, ResultStatus::accessError);
	SIM_EXPECT_EQ(fixture.target.beginCount, 1);
	StatusAnswer status = ReadStatus(fixture.sim);
	SIM_EXPECT(status.valid);
	SIM_EXPECT_EQ(status.state, DfuState::dfuERROR);
	SIM_EXPECT_EQ(status.status, DfuStatus::errSTALLEDPKT);
}


// A zero-length download in dfuDNLOAD-SYNC is neither the end of the download nor a block: it stalls, and the block that is pending is
// not written and does not start a manifestation.
void CheckZeroLengthDownloadInSyncStalls() {
	ModeDevice<ModeUsb> fixture;
	fixture.Configure();
	Download(fixture.sim, Pattern(64, 0));
	SIM_EXPECT_EQ(SendNoData(fixture.sim, RequestDnload, 0), ResultStatus::accessError);
	StatusAnswer status = ReadStatus(fixture.sim);
	SIM_EXPECT_EQ(status.state, DfuState::dfuERROR);
	SIM_EXPECT_EQ(status.status, DfuStatus::errSTALLEDPKT);
	SIM_EXPECT(fixture.target.writes.empty());
	SIM_EXPECT_EQ(fixture.target.manifestCount, 0);
}


// A DFU_UPLOAD asks for more than wTransferSize. The device answers wTransferSize bytes, which is shorter than the request: that is the
// end of the upload (appendix A.2.2), so the device is in dfuIDLE.
void CheckUploadLongerRequestEndsOnShortFrame() {
	ModeDevice<ModeUsb> fixture;
	fixture.Configure();
	fixture.target.image = Pattern(150, 0x00);

	ResultStatus result = ResultStatus::ok;
	std::vector<uint8> first = Upload(fixture.sim, 100, result);
	SIM_EXPECT_EQ(result, ResultStatus::ok);
	SIM_EXPECT_EQ(first.size(), 64);
	SIM_EXPECT(first == Pattern(64, 0x00));
	SIM_EXPECT_EQ(ReadState(fixture.sim), DfuState::dfuIDLE);
}


// Starts a manifestation: a block is written, the zero-length download is taken, and the first GETSTATUS starts Manifest() (dfuMANIFEST).
void StartManifest(ModeDevice<ModeUsb>& fixture) {
	DownloadAndWrite(fixture, Pattern(64, 0));
	SIM_EXPECT_EQ(SendNoData(fixture.sim, RequestDnload, 0), ResultStatus::ok);
	SIM_EXPECT_EQ(ReadStatus(fixture.sim).state, DfuState::dfuMANIFEST);
}


// While Manifest() runs, DFU_GETSTATUS answers the busy state with the poll timeout, not a stall. After the manifestation the tolerant
// device answers dfuIDLE.
void CheckManifestBusyAnswersStatusOnly() {
	ModeDevice<ModeUsb> fixture;
	fixture.Configure();
	StartManifest(fixture);

	StatusAnswer busy;
	fixture.target.duringManifest = [&fixture, &busy]() {
		busy = ReadStatus(fixture.sim);
	};
	fixture.device.Process();
	fixture.target.duringManifest = nullptr;

	SIM_EXPECT(busy.valid);
	SIM_EXPECT_EQ(busy.state, DfuState::dfuMANIFEST);
	SIM_EXPECT_EQ(busy.status, DfuStatus::OK);
	SIM_EXPECT_EQ(busy.pollTimeoutMs, 40);
	SIM_EXPECT_EQ(fixture.target.manifestCount, 1);
	SIM_EXPECT_EQ(ReadStatus(fixture.sim).state, DfuState::dfuIDLE);
}


// DFU_DNLOAD, DFU_UPLOAD and DFU_GETSTATE sent while Manifest() runs each stall, and the device goes to dfuERROR. The manifestation
// result is then dropped, so the state stays dfuERROR with errSTALLEDPKT. Each request gets its own device: after the first stall the
// device is in dfuERROR, where GETSTATE is answered.
void CheckManifestBusyStallsOtherRequests() {
	ModeDevice<ModeUsb> download;
	download.Configure();
	StartManifest(download);
	ResultStatus downloadResult = ResultStatus::ok;
	std::vector<uint8> block = Pattern(64, 0x40);
	download.target.duringManifest = [&download, &block, &downloadResult]() {
		downloadResult = SendOut(download.sim, RequestDnload, 0, block);
	};
	download.device.Process();
	download.target.duringManifest = nullptr;
	SIM_EXPECT_EQ(downloadResult, ResultStatus::accessError);
	StatusAnswer downloadStatus = ReadStatus(download.sim);
	SIM_EXPECT_EQ(downloadStatus.state, DfuState::dfuERROR);
	SIM_EXPECT_EQ(downloadStatus.status, DfuStatus::errSTALLEDPKT);

	ModeDevice<ModeUsb> upload;
	upload.Configure();
	upload.target.image = Pattern(150, 0x00);
	StartManifest(upload);
	ResultStatus uploadResult = ResultStatus::ok;
	upload.target.duringManifest = [&upload, &uploadResult]() {
		uploadResult = GetIn(upload.sim, RequestUpload, 0, 64).status;
	};
	upload.device.Process();
	upload.target.duringManifest = nullptr;
	SIM_EXPECT_EQ(uploadResult, ResultStatus::accessError);
	StatusAnswer uploadStatus = ReadStatus(upload.sim);
	SIM_EXPECT_EQ(uploadStatus.state, DfuState::dfuERROR);
	SIM_EXPECT_EQ(uploadStatus.status, DfuStatus::errSTALLEDPKT);

	ModeDevice<ModeUsb> state;
	state.Configure();
	StartManifest(state);
	ResultStatus stateResult = ResultStatus::ok;
	state.target.duringManifest = [&state, &stateResult]() {
		stateResult = GetIn(state.sim, RequestGetState, 0, 1).status;
	};
	state.device.Process();
	state.target.duringManifest = nullptr;
	SIM_EXPECT_EQ(stateResult, ResultStatus::accessError);
	StatusAnswer stateStatus = ReadStatus(state.sim);
	SIM_EXPECT_EQ(stateStatus.state, DfuState::dfuERROR);
	SIM_EXPECT_EQ(stateStatus.status, DfuStatus::errSTALLEDPKT);
}


// The core can refuse to send a control answer. The function then reports the failure as errUNKNOWN and the request stalls, instead of
// completing it without its data.
void CheckFailedSendStalls() {
	FakeDfuTarget target;
	target.image = Pattern(150, 0x00);
	DfuFunction<64> function(DfuTest::ModeUsb::Function(0), target);
	FailingSendContext context;
	context.sendFails = true;
	SIM_EXPECT_EQ(function.OnConfigured(context), ResultStatus::ok);

	ResultStatus status = function.OnControlRequest(USBSetupPacket::Parse(SimHost::MakeSetup(0xA1, RequestGetStatus, 0, 0, 6)), context);
	SIM_EXPECT_EQ(status, ResultStatus::notSupported);
	SIM_EXPECT(function.GetState() == DfuState::dfuERROR);
	SIM_EXPECT(function.GetStatus() == DfuStatus::errUNKNOWN);

	DfuFunction<64> state(DfuTest::ModeUsb::Function(0), target);
	SIM_EXPECT_EQ(state.OnConfigured(context), ResultStatus::ok);
	status = state.OnControlRequest(USBSetupPacket::Parse(SimHost::MakeSetup(0xA1, RequestGetState, 0, 0, 1)), context);
	SIM_EXPECT_EQ(status, ResultStatus::notSupported);
	SIM_EXPECT(state.GetState() == DfuState::dfuERROR);
	SIM_EXPECT(state.GetStatus() == DfuStatus::errUNKNOWN);

	DfuFunction<64> upload(DfuTest::ModeUsb::Function(0), target);
	SIM_EXPECT_EQ(upload.OnConfigured(context), ResultStatus::ok);
	status = upload.OnControlRequest(USBSetupPacket::Parse(SimHost::MakeSetup(0xA1, RequestUpload, 0, 0, 64)), context);
	SIM_EXPECT_EQ(status, ResultStatus::notSupported);
	SIM_EXPECT(upload.GetState() == DfuState::dfuERROR);
	SIM_EXPECT(upload.GetStatus() == DfuStatus::errUNKNOWN);
}


// The layout of a DFU function holds exactly one interface: the function reads that one to start its alternate. A layout without one
// (or with two) is refused at configuration, so no request reads past the span.
void CheckLayoutNeedsOneInterface() {
	FakeDfuTarget target;
	FailingSendContext context;
	static const std::array<uint8, 2> twoInterfaces{ 0, 1 };

	USBFunctionLayout none = DfuTest::ModeUsb::Function(0);
	none.interfaces = std::span<const uint8>();
	DfuFunction<64> noInterface(none, target);
	SIM_EXPECT_EQ(noInterface.OnConfigured(context), ResultStatus::invalidParameter);

	USBFunctionLayout two = DfuTest::ModeUsb::Function(0);
	two.interfaces = std::span<const uint8>(twoInterfaces);
	DfuFunction<64> twoInterface(two, target);
	SIM_EXPECT_EQ(twoInterface.OnConfigured(context), ResultStatus::invalidParameter);
}


void CheckHostResetRestartsAfterManifestWithoutTolerance() {
	ModeDevice<WaitResetUsb> fixture;
	fixture.Configure();
	fixture.function.SetDetachDelegate(DfuFunction<64>::DetachDelegate::Bind<&OnDetach>());
	detachCalls = 0;

	DownloadAndWrite(fixture, Pattern(64, 0));
	SIM_EXPECT_EQ(SendNoData(fixture.sim, RequestDnload, 0), ResultStatus::ok);
	ReadStatus(fixture.sim);
	fixture.device.Process();
	SIM_EXPECT_EQ(fixture.function.GetState(), DfuState::dfuMANIFEST_WAIT_RESET);
	SIM_EXPECT_EQ(detachCalls, 0);

	// The host's reset is the signal to leave the loader: the application is asked to restart once, and no detach is claimed.
	fixture.sim.InjectBusReset();
	fixture.device.Process();
	SIM_EXPECT_EQ(detachCalls, 1);
	SIM_EXPECT(!lastWillDetach);
	fixture.device.Process();
	SIM_EXPECT_EQ(detachCalls, 1);
	SIM_EXPECT_EQ(fixture.function.GetState(), DfuState::dfuIDLE);
}


}


int main() {
	CheckDownloadHappyPath();
	CheckBusyPollReportsAndClamp();
	CheckWriteFailure(DfuStatus::errWRITE);
	CheckWriteFailure(DfuStatus::errPROG);
	CheckWriteFailure(DfuStatus::errVERIFY);
	CheckWriteFailure(DfuStatus::errADDRESS);
	CheckWriteFailure(DfuStatus::errCHECK_ERASED);
	CheckWriteErrorIsStickyUntilClear();
	CheckBeginFailureIsTargetError();
	CheckZeroLengthDownloadInIdleStalls();
	CheckZeroLengthNotDoneIsErrNotDone();
	CheckOversizedBlockStalls();
	CheckDownloadInSyncStalls();
	CheckAbortRules();
	CheckClearStatusInIdleStalls();
	CheckUnknownRequestsStall();
	CheckUploadOnlyRefusesDownload();
	CheckDownloadOnlyRefusesUpload();
	CheckUploadEndsOnShortBlock();
	CheckUploadExactMultipleEndsWithEmptyBlock();
	CheckUploadAbort();
	CheckUploadReadErrorStalls();
	CheckManifestWithoutToleranceWaitsForReset();
	CheckManifestWithWillDetachAsksForDetach();
	CheckManifestErrorIsReported();
	CheckBusResetDuringWriteDiscardsIt();
	CheckStallDuringWriteKeepsError();
	CheckStandardRequestKeepsState();
	CheckReconfigureDuringWriteDiscardsIt();
	CheckBusResetDuringManifestDiscardsIt();
	CheckDownloadDuringWriteStalls();
	CheckZeroLengthDownloadInSyncStalls();
	CheckUploadLongerRequestEndsOnShortFrame();
	CheckManifestBusyAnswersStatusOnly();
	CheckManifestBusyStallsOtherRequests();
	CheckFailedSendStalls();
	CheckLayoutNeedsOneInterface();
	CheckHostResetRestartsAfterManifestWithoutTolerance();
	return SimTest::Finish("TestDfuState");
}
