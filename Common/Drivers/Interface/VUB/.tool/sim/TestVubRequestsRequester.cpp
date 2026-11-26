#include "RequestsChains.h"
#include <Drivers/Interface/VUB/Outbox.h>
#include <Drivers/Interface/VUB/Requests.h>
#include <Utilities/DataTypes.h>
#include <array>
#include <cstddef>
#include <span>

// The requester side of the requests layer: the replies it takes, its deadlines and retries, its table of slots and numbers, and the
// interims it hands to its handlers. The stacks and the commands are in RequestsChains.h.


// The interim recorder of the ten-interim test: the status of each interim, in arrival order.
inline std::size_t tenCount = 0;
inline std::array<uint8, 16> tenStatuses{};


void OnTenInterim(const VUB::Progress<Motor::MotorStatus>& progress) {
	if (tenCount < tenStatuses.size()) {
		tenStatuses[tenCount] = static_cast<uint8>(progress.status);
	}

	tenCount++;
}


// Interim replies arrive in order, ahead of the final; the last one is the device's own.
void TestInterimReplies() {
	ResetRequestsState();
	ReportingPacketLine line(VUB::Faults{});
	Requester near(line.a);
	Responder far(line.b);
	WireLog log;
	Establish(line, near, far, log);
	startInterimAtOnce = true;

	auto handle = near.Requests();
	SIM_EXPECT(SendStartR(handle));
	Run(line, near, far, log, 1000);
	SIM_EXPECT(startInterims == 1);
	SIM_EXPECT(lastInterimStatus == Motor::MotorStatus::warming);
	SIM_EXPECT(startFinals == 0);

	SIM_EXPECT(startHeld);
	SIM_EXPECT(heldStart.Interim(Motor::MotorStatus::started) == ResultStatus::ok);
	Run(line, near, far, log, 2000);
	SIM_EXPECT(startInterims == 2);
	SIM_EXPECT(lastInterimStatus == Motor::MotorStatus::started);

	Motor::MotorReport report{};
	report.speedRpm = 1200;
	report.currentMa = 850;
	SIM_EXPECT(heldStart.Finish(Motor::MotorStatus::started, report) == ResultStatus::ok);
	Run(line, near, far, log, 3000);
	SIM_EXPECT(startFinals == 1);
	SIM_EXPECT(startResult == ResultStatus::ok);
	SIM_EXPECT(startStatus == Motor::MotorStatus::started);
	SIM_EXPECT(startReport.speedRpm == 1200);
	SIM_EXPECT(startReport.currentMa == 850);
	SIM_EXPECT(startInterims == 2);
}


// The requester's deadline: the device holds the request and never answers, so the request ends with timeout. A late final is dropped.
void TestRequesterTimeoutAndLateFinal() {
	ResetRequestsState();
	ReportingPacketLine line(VUB::Faults{});
	Requester near(line.a);
	Responder far(line.b);
	WireLog log;
	Establish(line, near, far, log);

	auto handle = near.Requests();
	SIM_EXPECT(SendStopR(handle));
	uint32 now = 1000;
	for (int step = 0; step < 25; step++) {
		Run(line, near, far, log, now);
		now += 100000;
	}

	SIM_EXPECT(stopFinals == 1);
	SIM_EXPECT(stopResult == ResultStatus::timeout);
	// The device's ticket is closed by its own deadline (or by the requester's final timeout): a late finish is notAvailable, and no second
	// final reaches the requester.
	SIM_EXPECT(heldStop.Open() == false);
	SIM_EXPECT(heldStop.Finish(Motor::MotorStatus::started) == ResultStatus::notAvailable);
	Run(line, near, far, log, now);
	SIM_EXPECT(stopFinals == 1);
}


// Retries end a request with retryExhausted: the wire is cut after the session starts, so no acknowledgement comes back.
void TestRetryExhausted() {
	ResetRequestsState();
	ReportingPacketLine line(VUB::Faults{});
	Requester near(line.a);
	Responder far(line.b);
	WireLog log;
	Establish(line, near, far, log);

	auto handle = near.Requests();
	SIM_EXPECT(SendStopR(handle));
	uint32 now = 1000;
	for (int step = 0; step < 12; step++) {
		testNowUs = now;
		near.Process();
		now += 100000;
	}

	SIM_EXPECT(stopFinals == 1);
	SIM_EXPECT(stopResult == ResultStatus::retryExhausted);
}


// Interim and final of a request the device finishes at once, with the start's report: the requester sees the status and the body.
void TestFinalWithReport() {
	ResetRequestsState();
	ReportingPacketLine line(VUB::Faults{});
	Requester near(line.a);
	Responder far(line.b);
	WireLog log;
	Establish(line, near, far, log);
	startAtOnce = true;

	auto handle = near.Requests();
	SIM_EXPECT(SendStartR(handle));
	Run(line, near, far, log, 1000);
	SIM_EXPECT(startFinals == 1);
	SIM_EXPECT(startReport.speedRpm == 1200);
	SIM_EXPECT(startReport.currentMa == 850);
	SIM_EXPECT(startInterims == 0);
}


// The full table, the number wrap with a late final, and a busy send that uses no number. These need no stack: a requester layer with
// a recording outbox, and the packets it sends and takes are hand-written.
// Two slots: the third send is busy, and it uses no number, so the send after the first final takes number 3.
void TestFullTableAndBusySend() {
	tableFinals = 0;
	TableLayer layer;
	Probe probe;
	TableLayer::Handle handle(layer, ProbeOutbox(probe));
	auto first =handle.Send<RequesterCommand::stop,&OnTableFinal>(VUB::NoBody{});
	auto second = handle.Send<RequesterCommand::stop,&OnTableFinal>(VUB::NoBody{});
	SIM_EXPECT(first.IsErr() == false);
	SIM_EXPECT(second.IsErr() == false);
	SIM_EXPECT(first.Value().number == 1);
	SIM_EXPECT(second.Value().number == 2);
	auto third = handle.Send<RequesterCommand::stop,&OnTableFinal>(VUB::NoBody{});
	SIM_EXPECT(third.IsErr());
	SIM_EXPECT(third.Error() == ResultStatus::busy);

	std::array<uint8, 6> finalOne = FinalOk(1, 1);
	SIM_EXPECT(layer.Take(VUB::Meta(), std::span<const uint8>(finalOne.data(), finalOne.size())).IsErr() == false);
	layer.Process(1000, ProbeOutbox(probe));
	SIM_EXPECT(tableFinals == 1);
	SIM_EXPECT(tableResult == ResultStatus::ok);

	// The stack refuses the next send once: no number is used, and the send after it gets number 3.
	probe.answer = ResultStatus::busy;
	auto refused = handle.Send<RequesterCommand::stop,&OnTableFinal>(VUB::NoBody{});
	SIM_EXPECT(refused.IsErr());
	SIM_EXPECT(refused.Error() == ResultStatus::busy);
	probe.answer = ResultStatus::ok;
	auto third2 = handle.Send<RequesterCommand::stop,&OnTableFinal>(VUB::NoBody{});
	SIM_EXPECT(third2.IsErr() == false);
	SIM_EXPECT(third2.Value().number == 3);
	SIM_EXPECT(probe.length == 5);
	std::array<uint8, 5> stopThree = {0x01, 0x13, 0x03, 0x00, 0x03};
	for (std::size_t index = 0; index < stopThree.size(); index++) {
		SIM_EXPECT(probe.last[index] == stopThree[index]);
	}
}


// The number wraps after 65535 requests: the next one is number 1 with the tag the counter gives (0), not the tag of the old number 1
// (1). A late final of the old request, with its old tag, does not close the second one; the second one's final does.
void TestNumberWrapWithLateFinal() {
	tableFinals = 0;
	TableLayer layer;
	Probe probe;
	TableLayer::Handle handle(layer, ProbeOutbox(probe));
	for (uint32 count = 0; count < 65535; count++) {
		auto sent = handle.Send<RequesterCommand::stop,&OnTableFinal>(VUB::NoBody{});
		SIM_EXPECT(sent.IsErr() == false);
		uint16 number = sent.Value().number;
		uint8 tag = static_cast<uint8>(probe.last[4]);
		std::array<uint8, 6> answer = FinalOk(number, tag);
		(void)layer.Take(VUB::Meta(), std::span<const uint8>(answer.data(), 6));
		layer.Process(1000, ProbeOutbox(probe));
	}

	SIM_EXPECT(tableFinals == 65535);
	auto wrapped = handle.Send<RequesterCommand::stop,&OnTableFinal>(VUB::NoBody{});
	SIM_EXPECT(wrapped.IsErr() == false);
	SIM_EXPECT(wrapped.Value().number == 1);
	std::array<uint8, 5> stopOne = {0x01, 0x13, 0x01, 0x00, 0x00};
	for (std::size_t index = 0; index < stopOne.size(); index++) {
		SIM_EXPECT(probe.last[index] == stopOne[index]);
	}

	std::array<uint8, 6> lateOld = {0x03, 0x01, 0x00, 0x01, 0x00, 0x00};
	SIM_EXPECT(layer.Take(VUB::Meta(), std::span<const uint8>(lateOld.data(), lateOld.size())).IsErr() == false);
	layer.Process(2000, ProbeOutbox(probe));
	SIM_EXPECT(tableFinals == 65535);

	std::array<uint8, 6> newOne = {0x03, 0x01, 0x00, 0x00, 0x00, 0x00};
	SIM_EXPECT(layer.Take(VUB::Meta(), std::span<const uint8>(newOne.data(), newOne.size())).IsErr() == false);
	layer.Process(3000, ProbeOutbox(probe));
	SIM_EXPECT(tableFinals == 65536);
	SIM_EXPECT(tableResult == ResultStatus::ok);
}


// Ten interim replies taken in one burst: each handler runs at once, in arrival order, and none is lost.
void TestTenInterimsInOneProcess() {
	tenCount = 0;
	tenStatuses.fill(0);
	ChainLayerTwo layer;
	Probe probe;
	ChainLayerTwo::Handle handle(layer, ProbeOutbox(probe));
	auto start =handle.template Send<RequesterCommand::start,&OnChainStartFinal, &OnTenInterim>(Motor::MotorStart{});
	SIM_EXPECT(start.IsErr() == false);
	for (uint8 index = 0; index < 10; index++) {
		std::array<uint8, 6> interim = {0x02, 0x01, 0x00, 0x01, static_cast<uint8>(index + 1), static_cast<uint8>(10 + index)};
		SIM_EXPECT(layer.Take(VUB::Meta(), std::span<const uint8>(interim.data(), interim.size())).IsErr() == false);
	}

	layer.Process(1000, ProbeOutbox(probe));
	SIM_EXPECT(tenCount == 10);
	for (std::size_t index = 0; index < 10; index++) {
		SIM_EXPECT(tenStatuses[index] == static_cast<uint8>(10 + index));
	}
}


// A link the first Process has not sampled: a FailWhileDown send is refused with noConnection ahead of it reaching
// the layer below. A send of a command without FailWhileDown is refused by the layer below (noInit: no session has a boot value).
void TestSendBeforeLinkUpIsNoConnection() {
	ResetRequestsState();
	ReportingPacketLine line(VUB::Faults{});
	Requester near(line.a);
	auto handle = near.Requests();
	auto refused = handle.Send<RequesterCommand::stop,&OnStopFinalR>(VUB::NoBody{});
	SIM_EXPECT(refused.IsErr());
	SIM_EXPECT(refused.Error() == ResultStatus::noConnection);
	auto unrestricted = handle.Send<RequesterCommand::start,&OnStartFinalR>(Motor::MotorStart{});
	SIM_EXPECT(unrestricted.IsErr());
	SIM_EXPECT(unrestricted.Error() == ResultStatus::noInit);
}


// The session call is a member of a layer with no Reliable below only: the handle of a layer over Reliable has no SetSession.
template<typename HandleType>
constexpr bool HasSetSession = requires(HandleType& handle) {
	handle.SetSession(uint16{1});
};


using PlainTable = VUB::RequestsOf<RequesterCommands, VUB::Handlers<>, VUB::RequestsOptions{2, 0, 0, false}>;
static_assert(!HasSetSession<TableLayer::Handle>, "SetSession is a member only with no Reliable below");
static_assert(HasSetSession<PlainTable::Handle>, "SetSession is a member with no Reliable below");


int main() {
	TestInterimReplies();
	TestRequesterTimeoutAndLateFinal();
	TestRetryExhausted();
	TestFinalWithReport();
	TestFullTableAndBusySend();
	TestNumberWrapWithLateFinal();
	TestTenInterimsInOneProcess();
	TestSendBeforeLinkUpIsNoConnection();
	return SimFinish("TestVubRequestsRequester");
}
