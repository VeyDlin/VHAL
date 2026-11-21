#include <VHAL.h>
#include "DfuTestDevices.h"
#include "SimAssert.h"
#include "SimHost.h"
#include <Drivers/Interface/USB/Class/DFU/DfuFunction.h>
#include <Drivers/Interface/USB/Class/DFU/USBDfuProtocol.h>
#include <array>
#include <span>
#include <vector>


// The memory regions of a DFU mode device as alternate settings: SET_INTERFACE and GET_INTERFACE over the control pipe, the refusals
// of SET_INTERFACE (an alternate the descriptor does not list, a switch in the middle of a transfer), a download that lands in the
// region the host selected, and the return to alternate 0 on a bus reset and on reconfiguration.


using namespace DfuTest;


namespace {


constexpr uint8 RequestDnload = static_cast<uint8>(DfuRequest::Dnload);
constexpr uint8 RequestGetInterface = 10;
constexpr uint8 RequestSetInterface = 11;


// count bytes that start at start: start, start + 1, ...
std::vector<uint8> Block(std::size_t count, uint8 start) {
	std::vector<uint8> bytes(count);
	for (std::size_t index = 0; index < count; index++) {
		bytes[index] = static_cast<uint8>(start + index);
	}

	return bytes;
}


// SET_INTERFACE of interface 0 to alternate. A standard request to the interface. Returns the core's result: accessError when the
// core stalled the request.
ResultStatus SetInterface(SimUSBDeviceAdapter& sim, uint16 alternate) {
	return SimHost::Control(sim, SimHost::MakeSetup(0x01, RequestSetInterface, alternate, 0, 0));
}


// GET_INTERFACE of interface 0: the answer's status and the one byte the device sent.
Answer GetInterface(SimUSBDeviceAdapter& sim) {
	std::array<uint8, 1> buffer{};
	uint32 received = 0;
	Answer answer;
	answer.status = SimHost::Control(sim, SimHost::MakeSetup(0x81, RequestGetInterface, 0, 0, 1), std::span<const uint8>(), buffer, received);
	answer.data.assign(buffer.begin(), buffer.begin() + received);
	return answer;
}


// True when GET_INTERFACE answers alternate with one byte.
bool IsSelected(SimUSBDeviceAdapter& sim, uint8 alternate) {
	Answer answer = GetInterface(sim);
	return answer.status == ResultStatus::ok && answer.data.size() == 1 && answer.data[0] == alternate;
}


// Every alternate of the descriptor is selectable and GET_INTERFACE answers it; alternate 0 is the default after configuration.
void CheckSetAndGetInterfaceTwoRegions() {
	ModeDevice<TwoRegionUsb> fixture;
	fixture.Configure();
	SIM_EXPECT(IsSelected(fixture.sim, 0));

	SIM_EXPECT_EQ(SetInterface(fixture.sim, 1), ResultStatus::ok);
	SIM_EXPECT(IsSelected(fixture.sim, 1));

	SIM_EXPECT_EQ(SetInterface(fixture.sim, 0), ResultStatus::ok);
	SIM_EXPECT(IsSelected(fixture.sim, 0));
}


// An alternate the descriptor does not list stalls, and the selection stays where it was.
void CheckUnknownAlternateStalls() {
	ModeDevice<TwoRegionUsb> fixture;
	fixture.Configure();

	SIM_EXPECT_EQ(SetInterface(fixture.sim, 2), ResultStatus::accessError);
	SIM_EXPECT(IsSelected(fixture.sim, 0));
	SIM_EXPECT(ReadState(fixture.sim) == DfuState::dfuIDLE);

	SIM_EXPECT_EQ(SetInterface(fixture.sim, 1), ResultStatus::ok);
	SIM_EXPECT_EQ(SetInterface(fixture.sim, 255), ResultStatus::accessError);
	SIM_EXPECT(IsSelected(fixture.sim, 1));
}


// Three regions: each alternate is selectable, and the first one the device does not list stalls.
void CheckThreeRegionsEachSelectable() {
	ModeDevice<ThreeRegionUsb> fixture;
	fixture.Configure();

	for (uint8 alternate = 0; alternate < 3; alternate++) {
		SIM_EXPECT_EQ(SetInterface(fixture.sim, alternate), ResultStatus::ok);
		SIM_EXPECT(IsSelected(fixture.sim, alternate));
	}

	SIM_EXPECT_EQ(SetInterface(fixture.sim, 3), ResultStatus::accessError);
	SIM_EXPECT(IsSelected(fixture.sim, 2));
}


// A switch while a block is written, or after it is written but before the transfer ends, stalls: the rest of the stream would go to
// another region. The state stays dfuDNLOAD-IDLE.
void CheckSwitchRefusedMidTransfer() {
	ModeDevice<TwoRegionUsb> fixture;
	fixture.Configure();
	SIM_EXPECT_EQ(SetInterface(fixture.sim, 1), ResultStatus::ok);

	std::vector<uint8> block = Block(16, 0x10);
	SIM_EXPECT_EQ(SendOut(fixture.sim, RequestDnload, 0, block), ResultStatus::ok);
	SIM_EXPECT(ReadStatus(fixture.sim).state == DfuState::dfuDNBUSY);
	SIM_EXPECT_EQ(SetInterface(fixture.sim, 0), ResultStatus::accessError);
	fixture.device.Process();
	SIM_EXPECT(ReadStatus(fixture.sim).state == DfuState::dfuDNLOAD_IDLE);

	SIM_EXPECT_EQ(SetInterface(fixture.sim, 0), ResultStatus::accessError);
	SIM_EXPECT(ReadState(fixture.sim) == DfuState::dfuDNLOAD_IDLE);
	SIM_EXPECT(IsSelected(fixture.sim, 1));
}


// A download after SET_INTERFACE 1 is written to region 1: the target is begun with alternate 1 and receives the block at offset 0.
void CheckDownloadLandsInSelectedRegion() {
	ModeDevice<TwoRegionUsb> fixture;
	fixture.Configure();
	SIM_EXPECT_EQ(SetInterface(fixture.sim, 1), ResultStatus::ok);

	std::vector<uint8> block = Block(16, 0x40);
	SIM_EXPECT_EQ(SendOut(fixture.sim, RequestDnload, 0, block), ResultStatus::ok);
	SIM_EXPECT(ReadStatus(fixture.sim).state == DfuState::dfuDNBUSY);
	fixture.device.Process();
	SIM_EXPECT(ReadStatus(fixture.sim).state == DfuState::dfuDNLOAD_IDLE);

	SIM_EXPECT_EQ(fixture.target.beginCount, 1);
	SIM_EXPECT_EQ(fixture.target.lastAlternate, 1);
	SIM_EXPECT_EQ(fixture.target.writes.size(), 1);
	SIM_EXPECT(fixture.target.writes.size() == 1 && fixture.target.writes[0].offset == 0);
	SIM_EXPECT(fixture.target.writes.size() == 1 && fixture.target.writes[0].data == block);
}


// A bus reset and a reconfiguration both return the device to alternate 0. The selection is not kept across them.
void CheckResetAndReconfigureReturnToAlternateZero() {
	ModeDevice<TwoRegionUsb> fixture;
	fixture.Configure();

	SIM_EXPECT_EQ(SetInterface(fixture.sim, 1), ResultStatus::ok);
	Enumerate(fixture.sim);
	SIM_EXPECT(IsSelected(fixture.sim, 0));

	SIM_EXPECT_EQ(SetInterface(fixture.sim, 1), ResultStatus::ok);
	SIM_EXPECT_EQ(SetConfiguration(fixture.sim, 0), ResultStatus::ok);
	SIM_EXPECT_EQ(SetConfiguration(fixture.sim, 1), ResultStatus::ok);
	SIM_EXPECT(IsSelected(fixture.sim, 0));
}


}


int main() {
	CheckSetAndGetInterfaceTwoRegions();
	CheckUnknownAlternateStalls();
	CheckThreeRegionsEachSelectable();
	CheckSwitchRefusedMidTransfer();
	CheckDownloadLandsInSelectedRegion();
	CheckResetAndReconfigureReturnToAlternateZero();
	return SimTest::Finish("TestDfuAlternates");
}
