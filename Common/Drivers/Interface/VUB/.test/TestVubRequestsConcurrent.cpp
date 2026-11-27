// libs-linux: pthread
#include "Chains.h"
#include "SimCheck.h"
#include <Drivers/Interface/VUB/Layer/Reliable.h>
#include <Drivers/Interface/VUB/Requests.h>
#include <Drivers/Interface/VUB/Stack.h>
#include <System/System.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/Result.h>
#include <Utilities/Result/ResultStatus.h>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <mutex>
#include <optional>
#include <span>
#include <thread>
#include <utility>
#include <vector>

// The requests layer on a concurrent stack, with host threads. The requester and the responder are two stacks on a reporting packet line
// (Chains.h). The main thread runs the rounds: each round runs both stacks' Process and carries their packets across the line. Other threads
// send requests, finish tickets, cancel, emit events and call Await. The critical section is a recursive mutex, so the sections really
// exclude each other; a user handler must find no section open on its thread. Every wait is bounded, and a wait that cannot end stops the
// program with a message, so a defect fails the run and does not hang it.


// The critical sections of this program, as TestVubStackConcurrent.cpp does them: each enter locks the mutex, each leave unlocks it, on the
// thread that made the call. The depth is the number of sections the calling thread has open.
std::recursive_mutex sectionMutex;
std::atomic<uint32> sectionEnters{0};
thread_local int sectionDepth = 0;


// The window hook (see TestAwaitFinalBetweenTheCheckAndTheAbandon): the waiting task's second section entry after the arm is the abandon's. Before
// its lock, the hook opens the gate and waits until the other task has taken the final.
inline std::atomic<bool> windowPauseArm{false};
inline std::atomic<bool> windowArm{false};
inline std::atomic<bool> windowGate{false};
inline std::atomic<bool> windowDone{false};
inline std::atomic<uint32> windowSections{0};
inline thread_local bool isWaiterThread = false;


void System::CriticalSection(bool isEnable) {
	if (isEnable && isWaiterThread && windowArm.load() && windowSections.fetch_add(1) + 1 == 2) {
		windowArm.store(false);
		windowGate.store(true);
		uint32 spins = 0;
		while (windowDone.load() == false && spins < 2000000u) {
			spins++;
			std::this_thread::yield();
		}
	}

	if (isEnable) {
		sectionMutex.lock();
		sectionDepth++;
		sectionEnters.fetch_add(1);
	} else {
		sectionDepth--;
		sectionMutex.unlock();
	}
}


// The tick and the RTOS delay handle of the program's System. Await waits with WaitCondition, which reads the tick and calls the handle. The tick
// is moved only by the main thread (the abandoned-wait test moves it), and no RTOS handle is set, so the wait polls.
inline volatile uint32 System::tickLow = 0;
inline std::function<bool(uint32 delay)> System::rtosDelayMsHandle = nullptr;


// The clock of the program. Other threads never move it; the main thread does, between rounds.
inline std::atomic<uint32> nowUs{0};


inline uint32 ConcurrentClock() {
	return nowUs.load();
}


// Bounds: the rounds of a wait, the spins of a worker, and the retries of a send.
inline constexpr uint32 MaxRounds = 200000;
inline constexpr uint32 MaxSpins = 2000000;
inline constexpr uint32 MaxSendAttempts = 300000;
inline constexpr std::size_t Keys = 256;


// The boot values of the three sessions: the requester, the responder, and a responder that restarts with a different boot.
inline constexpr uint16 RequesterSession = 0x5A5A;
inline constexpr uint16 ResponderSession = 0x3C3C;
inline constexpr uint16 RestartedSession = 0x2B2B;


// A wait that cannot end (a defect) stops the program at once: the run fails with this message and does not hang.
[[noreturn]] void StopOnHang(const char* what) {
	std::printf("FAILED, the run stopped: %s\n", what);
	std::_Exit(1);
}


namespace Job {

enum class Command : uint8 {
	run = 0x21,
	hold = 0x22,
	note = 0x23
};


enum class Event : uint8 {
	tick = 0x40
};


// The request body of Command::run and Command::hold. The requester sets amount; the responder copies it into the report, so a final that carries
// a report shows which request it closes.
struct JobRequest {
	uint16 amount = 0;
};


struct JobReport {
	uint16 done = 0;
};


struct JobTick {
	uint32 count = 0;
};


enum class JobStatus : uint8 {
	started = 0,
	warming = 1
};

}


namespace VUB {

template<>
struct BodyOf<Job::JobRequest> {
	static consteval auto Layout() {
		return Fields<Job::JobRequest>()
			.Field<&Job::JobRequest::amount>();
	}
};


template<>
struct BodyOf<Job::JobReport> {
	static consteval auto Layout() {
		return Fields<Job::JobReport>()
			.Field<&Job::JobReport::done>();
	}
};


template<>
struct BodyOf<Job::JobTick> {
	static consteval auto Layout() {
		return Fields<Job::JobTick>()
			.Field<&Job::JobTick::count>();
	}
};

}


// The command set of this program. Command::run is cancellable and sends interim replies; Command::hold has a reply with no body and no interim;
// Event::tick is an event.
using JobCommands = VUB::CommandSet<+[]() {
	return VUB::Commands()
		.Command<Job::Command::run>()
			.Request<Job::JobRequest>()
			.Reply<Job::JobStatus, Job::JobReport>()
			.Interim()
			.Cancellable()
			.ReplyTimeoutMs(5000)
			.TicketTimeoutMs(60000)
		.Command<Job::Command::hold>()
			.Request<Job::JobRequest>()
			.Reply<Job::JobStatus>()
			.ReplyTimeoutMs(3000)
			.TicketTimeoutMs(60000)
		.Command<Job::Command::note>()
			.Request<Job::JobRequest>()
			.NoReply()
		.Event<Job::Event::tick>()
			.Body<Job::JobTick>();
}>;


using RunTicket = VUB::Ticket<JobCommands, Job::Command::run>;
using HoldTicket = VUB::Ticket<JobCommands, Job::Command::hold>;


// A request the responder has taken and not finished yet. The ticket is a value, so the list keeps a copy of it.
struct ParkedRun {
	RunTicket ticket;
	uint16 amount;
};


struct ParkedHold {
	HoldTicket ticket;
	uint16 amount;
};


// The parked requests, and the switches that park them. A parked request waits in its list until a test finishes it.
inline std::mutex parkMutex;
inline std::vector<ParkedRun> parkedRuns;
inline std::vector<ParkedHold> parkedHolds;
inline bool parkRuns = false;
inline bool parkHolds = false;
inline bool interimFirst = false;


// What the handlers counted. The handlers run on the main thread (from the rounds), so these need no lock; the tests read them after the
// threads they start have finished.
inline std::array<uint32, Keys> closedByKey{};
inline std::array<uint32, Keys> ticksSeen{};
inline uint32 finalsTotal = 0;
inline uint32 finalsNotOk = 0;
inline ResultStatus lastResult = ResultStatus::ok;
inline uint32 interimsTotal = 0;
inline uint32 holdFinals = 0;
inline uint32 tickCount = 0;
inline uint32 runHandlerCalls = 0;
inline uint32 holdHandlerCalls = 0;
// The amount and the request number of each Command::run the responder took, in handler order (the handler runs on the main thread).
struct RunArrival {
	uint16 amount;
	uint16 number;
};


inline std::vector<RunArrival> runArrivals;
inline uint32 cancelHandlerCalls = 0;
inline uint32 handlerDepthErrors = 0;


// A user handler must run with no critical section open on its thread.
inline void CheckOutsideSection() {
	if (sectionDepth != 0) {
		handlerDepthErrors++;
	}
}


// The requester's final of Command::run. An ok final carries the report, so its amount is the key of the request it closes. A final without a
// report (busy, timeout, operationCancelled) only counts.
void EchoOnFinal(uint16 amount);


// When echoFinals is set, the final of an amount below 40 sends one more request from inside Process (see EchoOnFinal).
inline bool echoFinals = false;


void OnRunFinal(const VUB::Final<Job::JobStatus, Job::JobReport>& reply) {
	CheckOutsideSection();
	finalsTotal++;
	lastResult = reply.result;
	if (reply.result == ResultStatus::ok) {
		closedByKey[reply.body.done % Keys]++;
		if (echoFinals) {
			EchoOnFinal(reply.body.done);
		}
	} else {
		finalsNotOk++;
	}
}


void OnRunInterim(const VUB::Progress<Job::JobStatus>& progress) {
	CheckOutsideSection();
	interimsTotal++;
	(void)progress;
}


void OnHoldFinal(const VUB::Final<Job::JobStatus, VUB::NoBody>& reply) {
	CheckOutsideSection();
	holdFinals++;
	lastResult = reply.result;
}


void OnTick(const Job::JobTick& tick) {
	CheckOutsideSection();
	tickCount++;
	if (tick.count < Keys) {
		ticksSeen[tick.count]++;
	}
}


void ConcurrentReceive(const VUB::Meta&, std::span<const uint8>) {
	CheckOutsideSection();
}


// The responder's handlers. Command::run is answered at once, or parked (its ticket is kept) when parkRuns is set; Command::hold is the same with
// parkHolds. A cancel finishes the ticket with operationCancelled.
void OnRun(RunTicket ticket, const Job::JobRequest& body) {
	CheckOutsideSection();
	runHandlerCalls++;
	runArrivals.push_back(RunArrival{body.amount, ticket.Number().number});
	if (interimFirst) {
		(void)ticket.Interim(Job::JobStatus::warming);
	}

	if (parkRuns) {
		std::lock_guard<std::mutex> guard(parkMutex);
		parkedRuns.push_back(ParkedRun{ticket, body.amount});
		return;
	}

	Job::JobReport report{};
	report.done = body.amount;
	(void)ticket.Finish(Job::JobStatus::started, report);
}


void OnRunCancel(RunTicket ticket) {
	CheckOutsideSection();
	cancelHandlerCalls++;
	(void)ticket.Cancelled();
}


void OnHold(HoldTicket ticket, const Job::JobRequest& body) {
	CheckOutsideSection();
	holdHandlerCalls++;
	if (parkHolds) {
		std::lock_guard<std::mutex> guard(parkMutex);
		parkedHolds.push_back(ParkedHold{ticket, body.amount});
		return;
	}

	(void)ticket.Finish(Job::JobStatus::started);
}


// The handler of Command::note: it has no reply, so it runs in the walk that takes the request.
inline uint32 noteHandlerCalls = 0;


void OnNote(const Job::JobRequest& body) {
	CheckOutsideSection();
	noteHandlerCalls++;
	(void)body;
}


using ResponderHandlers = VUB::Handlers<
	VUB::On<Job::Command::run, &OnRun>,
	VUB::OnCancel<Job::Command::run, &OnRunCancel>,
	VUB::On<Job::Command::hold, &OnHold>,
	VUB::On<Job::Command::note, &OnNote>>;


using RequesterHandlers = VUB::Handlers<
	VUB::OnEvent<Job::Event::tick, &OnTick>>;


// The requester: four slots, the Reliable layer under it, and the sections. The requester's events are handled by RequesterHandlers. Await
// needs Awaitable(); Counted() gives the layer's refused and late-reply counters.
// --- snippet: Layer/Requests.md, the concurrent requester ---
using Requester = VUB::Stack<+[]() {
	return VUB::Builder()
		.CountDrops()
		.Layer<VUB::Requests<JobCommands, RequesterHandlers>>().Open(4).Awaitable().Counted()
		.Layer<VUB::ReliableOf<40, 4>>().Retries(3).TimeoutMs(200).Window(4)
		.Link<ReportingPacketEnd>()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.OnReceive<&ConcurrentReceive>()
		.Clock<&ConcurrentClock>()
		.Concurrent();
}>;
// --- end snippet ---


// The responder: four tickets, the same Reliable layer. The requester has four slots, so the requests in flight never need more than four
// tickets, and a responder that answers at once never refuses one of them busy.
using Responder = VUB::Stack<+[]() {
	return VUB::Builder()
		.CountDrops()
		.Layer<VUB::Requests<JobCommands, ResponderHandlers>>().Tickets(4)
		.Layer<VUB::ReliableOf<40, 4>>().Retries(3).TimeoutMs(200).Window(4)
		.Link<ReportingPacketEnd>()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.OnReceive<&ConcurrentReceive>()
		.Clock<&ConcurrentClock>()
		.Concurrent();
}>;


// The responder with three tickets, for the test that fills the responder's table: a fourth request is refused busy by the responder.
using SmallResponder = VUB::Stack<+[]() {
	return VUB::Builder()
		.CountDrops()
		.Layer<VUB::Requests<JobCommands, ResponderHandlers>>().Tickets(3)
		.Layer<VUB::ReliableOf<40, 4>>().Retries(3).TimeoutMs(200).Window(4)
		.Link<ReportingPacketEnd>()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.OnReceive<&ConcurrentReceive>()
		.Clock<&ConcurrentClock>()
		.Concurrent();
}>;


// The handles of the two layers: each thread gets its own copy.
using RequesterHandle = decltype(std::declval<Requester&>().Requests());
using ResponderHandle = decltype(std::declval<Responder&>().Requests());


// Sends the packets of one direction to the stack at the far end. Feed is not used by the line here.
template<typename StackType>
struct ToStack {
	StackType* target;


	void Feed(uint8) {
	}


	void FeedPacket(std::span<const uint8> packet) {
		target->FeedPacket(packet);
	}
};


// One round: the clock moves by stepUs, the requester runs, its packets reach the responder, the responder runs, its packets reach the
// requester, and the requester runs.
template<typename NearStack, typename FarStack>
void RunRound(ReportingPacketLine& line, NearStack& near, FarStack& far, uint32 stepUs) {
	nowUs.fetch_add(stepUs);
	near.Process();
	ToStack<FarStack> toFar{&far};
	line.DeliverToB(toFar);
	far.Process();
	ToStack<NearStack> toNear{&near};
	line.DeliverToA(toNear);
	near.Process();
}


template<typename NearStack, typename FarStack>
void RunRounds(ReportingPacketLine& line, NearStack& near, FarStack& far, uint32 stepUs, uint32 count) {
	for (uint32 round = 0; round < count; round++) {
		RunRound(line, near, far, stepUs);
	}
}


// Runs rounds until the condition holds or maxRounds rounds have run. Returns whether the condition holds at the end.
template<typename NearStack, typename FarStack, typename Condition>
bool RunUntil(ReportingPacketLine& line, NearStack& near, FarStack& far, uint32 stepUs, uint32 maxRounds, Condition condition) {
	for (uint32 round = 0; round < maxRounds; round++) {
		if (condition()) {
			return true;
		}

		RunRound(line, near, far, stepUs);
	}

	return condition();
}


// Gives the Reliable layer of a stack its boot value, as the application does before the first send.
template<typename StackType>
void GiveSession(StackType& stack, uint16 value) {
	stack.template Find<VUB::ReliableOf<40, 4>>().SetSession(value);
}


inline std::size_t ParkedRunCount() {
	std::lock_guard<std::mutex> guard(parkMutex);
	return parkedRuns.size();
}


inline std::size_t ParkedHoldCount() {
	std::lock_guard<std::mutex> guard(parkMutex);
	return parkedHolds.size();
}


// Takes one parked Command::run out of the list. The lock is released before the caller uses the ticket.
std::optional<ParkedRun> TakeParkedRun() {
	std::lock_guard<std::mutex> guard(parkMutex);
	if (parkedRuns.empty()) {
		return std::nullopt;
	}

	std::optional<ParkedRun> taken = parkedRuns.back();
	parkedRuns.pop_back();
	return taken;
}


std::optional<ParkedHold> TakeParkedHold() {
	std::lock_guard<std::mutex> guard(parkMutex);
	if (parkedHolds.empty()) {
		return std::nullopt;
	}

	std::optional<ParkedHold> taken = parkedHolds.back();
	parkedHolds.pop_back();
	return taken;
}


// One send of Command::run, with no retry. The result is returned as the layer gives it.
Result<VUB::RequestId> SendRunOnce(RequesterHandle& handle, uint16 amount) {
	Job::JobRequest body{};
	body.amount = amount;
	return handle.Send<Job::Command::run, &OnRunFinal, &OnRunInterim>(body);
}


// One send of Command::hold, with no retry.
Result<VUB::RequestId> SendHoldOnce(RequesterHandle& handle, uint16 amount) {
	Job::JobRequest body{};
	body.amount = amount;
	return handle.Send<Job::Command::hold, &OnHoldFinal>(body);
}


// The sessions of both ends are given, and the rounds run until the sync of each is acknowledged. A concurrent send is accepted into the
// queue at once (ok), and Process sends it when the sync is acknowledged, so no send is refused here for the sync.
template<typename FarStack>
void Establish(ReportingPacketLine& line, Requester& near, FarStack& far) {
	GiveSession(near, RequesterSession);
	GiveSession(far, ResponderSession);
	RunRounds(line, near, far, 50, 8);
}


// Sends one Command::run from another thread and retries while the table or the staging ring is busy. Returns false when the send is refused
// for another reason, or when the retries run out.
bool SendRunUntilAccepted(RequesterHandle handle, uint16 amount) {
	for (uint32 attempt = 0; attempt < MaxSendAttempts; attempt++) {
		Result<VUB::RequestId> sent = SendRunOnce(handle, amount);
		if (sent.IsErr() == false) {
			return true;
		}

		if (sent.Error() != ResultStatus::busy) {
			return false;
		}

		std::this_thread::yield();
	}

	return false;
}


// --- snippet: Layer/Requests.md, the await ---
// Await of Command::run, called from the calling thread. The final is returned to the caller; no final handler runs for it.
VUB::Final<Job::JobStatus, Job::JobReport> AwaitRun(RequesterHandle handle, uint16 amount) {
	Job::JobRequest body{};
	body.amount = amount;
	return handle.Await<Job::Command::run, &OnRunInterim>(body);
}
// --- end snippet ---


// Await of Command::hold, called from the calling thread.
VUB::Final<Job::JobStatus, VUB::NoBody> AwaitHold(RequesterHandle handle, uint16 amount) {
	Job::JobRequest body{};
	body.amount = amount;
	return handle.Await<Job::Command::hold>(body);
}


// The requester whose final handler sends (EchoOnFinal) and the handle it sends with: set by the test that uses it, on the main thread.
RequesterHandle* echoTarget = nullptr;
inline uint32 echoSent = 0;


// Sends one Command::run from inside the final handler, which runs in Process. Amounts from 40 up do not echo, so each echo ends.
void EchoOnFinal(uint16 amount) {
	if (echoTarget == nullptr || amount >= 40) {
		return;
	}

	if (SendRunOnce(*echoTarget, static_cast<uint16>(amount + 200)).IsErr() == false) {
		echoSent++;
	}
}


// Clears the counters and the parked requests before a test. Each test sets the switches it needs.
void ResetCounters() {
	nowUs.store(1000);
	closedByKey.fill(0);
	ticksSeen.fill(0);
	finalsTotal = 0;
	finalsNotOk = 0;
	lastResult = ResultStatus::ok;
	interimsTotal = 0;
	holdFinals = 0;
	tickCount = 0;
	runHandlerCalls = 0;
	holdHandlerCalls = 0;
	cancelHandlerCalls = 0;
	runArrivals.clear();
	noteHandlerCalls = 0;
	echoFinals = false;
	echoTarget = nullptr;
	echoSent = 0;
	parkRuns = false;
	parkHolds = false;
	interimFirst = false;
	{
		std::lock_guard<std::mutex> guard(parkMutex);
		parkedRuns.clear();
		parkedHolds.clear();
	}
}


// Two producer threads send Command::run requests while the main thread runs the rounds. Each request is accepted once and closed once, with
// an ok final that names it.
void TestTwoSendersWhileRoundsRun() {
	ResetCounters();
	ReportingPacketLine line(VUB::Faults{});
	Requester near(line.a);
	Responder far(line.b);
	Establish(line, near, far);
	RequesterHandle handle = near.Requests();

	std::atomic<uint32> sendFailures{0};
	std::vector<std::thread> producers;
	for (uint16 producer = 0; producer < 2; producer++) {
		uint16 first = static_cast<uint16>(producer * 128);
		producers.emplace_back([handle, first, &sendFailures]() {
			for (uint16 index = 0; index < 60; index++) {
				if (SendRunUntilAccepted(handle, static_cast<uint16>(first + index)) == false) {
					sendFailures++;
				}
			}
		});
	}

	bool done = RunUntil(line, near, far, 100, MaxRounds, []() { return finalsTotal >= 120; });
	for (std::thread& producer : producers) {
		producer.join();
	}

	RunRounds(line, near, far, 100, 20);
	SIM_EXPECT(done);
	SIM_EXPECT(sendFailures.load() == 0);
	SIM_EXPECT(finalsTotal == 120);
	SIM_EXPECT(finalsNotOk == 0);
	for (uint16 producer = 0; producer < 2; producer++) {
		for (uint16 index = 0; index < 60; index++) {
			SIM_EXPECT(closedByKey[static_cast<uint16>(producer * 128 + index)] == 1);
		}
	}

	// Every request of the run reached the responder once. The order of the sends of one task is the order of the queue (FIFO); the handlers of
	// one receive burst run by ticket slot, which the design does not order (only Fire and event against request order), so no handler order
	// is asserted here.
	SIM_EXPECT(runArrivals.size() == 120);

	SIM_EXPECT(sectionEnters.load() > 0);
}


// Three requests are parked by the responder. A worker thread finishes them: an interim and then the final of each, from its own thread.
void TestTicketsFinishedFromAnotherThread() {
	ResetCounters();
	ReportingPacketLine line(VUB::Faults{});
	Requester near(line.a);
	Responder far(line.b);
	Establish(line, near, far);
	RequesterHandle handle = near.Requests();

	parkRuns = true;
	for (uint16 amount = 0; amount < 3; amount++) {
		Result<VUB::RequestId> sent = SendRunOnce(handle, amount);
		SIM_EXPECT(sent.IsErr() == false);
	}

	bool parked = RunUntil(line, near, far, 100, MaxRounds, []() { return ParkedRunCount() == 3; });
	if (parked == false) {
		StopOnHang("the responder did not park the three requests");
	}

	std::atomic<uint32> finished{0};
	std::thread worker([&finished]() {
		uint32 spins = 0;
		while (finished.load() < 3 && spins < MaxSpins) {
			std::optional<ParkedRun> taken = TakeParkedRun();
			if (taken.has_value() == false) {
				spins++;
				std::this_thread::yield();
				continue;
			}

			Job::JobReport report{};
			report.done = taken->amount;
			(void)taken->ticket.Interim(Job::JobStatus::warming);
			(void)taken->ticket.Finish(Job::JobStatus::started, report);
			finished++;
		}
	});

	bool done = RunUntil(line, near, far, 100, MaxRounds, []() { return finalsTotal >= 3; });
	worker.join();
	RunRounds(line, near, far, 100, 5);
	SIM_EXPECT(done);
	SIM_EXPECT(finished.load() == 3);
	SIM_EXPECT(finalsTotal == 3);
	SIM_EXPECT(finalsNotOk == 0);
	SIM_EXPECT(interimsTotal == 3);
	for (uint16 amount = 0; amount < 3; amount++) {
		SIM_EXPECT(closedByKey[amount] == 1);
	}
}


// A parked Command::run is cancelled from another thread. The responder's cancel handler finishes its ticket with operationCancelled, and the
// requester's final is that result. Cancel returns ok for an open request; a second cancel of the same request also returns ok and sends
// nothing (the handler still runs once); a cancel after the request has closed returns notFound.
void TestCancelFromAnotherThread() {
	ResetCounters();
	ReportingPacketLine line(VUB::Faults{});
	Requester near(line.a);
	Responder far(line.b);
	Establish(line, near, far);
	RequesterHandle handle = near.Requests();

	parkRuns = true;
	Result<VUB::RequestId> sent = SendRunOnce(handle, 7);
	SIM_EXPECT(sent.IsErr() == false);
	if (sent.IsErr()) {
		StopOnHang("the request was not accepted");
	}

	VUB::RequestId id = sent.Value();
	bool parked = RunUntil(line, near, far, 100, MaxRounds, []() { return ParkedRunCount() == 1; });
	if (parked == false) {
		StopOnHang("the responder did not park the request");
	}

	ResultStatus firstCancel = ResultStatus::busy;
	std::thread canceller([handle, id, &firstCancel]() mutable {
		firstCancel = handle.Cancel<Job::Command::run>(id);
	});
	canceller.join();
	SIM_EXPECT(firstCancel == ResultStatus::ok);
	SIM_EXPECT(handle.Cancel<Job::Command::run>(id) == ResultStatus::ok);

	bool done = RunUntil(line, near, far, 100, MaxRounds, []() { return finalsTotal >= 1; });
	RunRounds(line, near, far, 100, 5);
	SIM_EXPECT(done);
	SIM_EXPECT(finalsTotal == 1);
	SIM_EXPECT(finalsNotOk == 1);
	SIM_EXPECT(lastResult == ResultStatus::operationCancelled);
	SIM_EXPECT(cancelHandlerCalls == 1);
	SIM_EXPECT(handle.Cancel<Job::Command::run>(id) == ResultStatus::notFound);
}


// Two parked requests are cancelled at once from another thread with CancelAll. Each cancel handler runs once and each request ends with
// operationCancelled. A request sent after CancelAll is not cancelled: it is answered at once and closes with ok.
void TestCancelAllFromAnotherThread() {
	ResetCounters();
	ReportingPacketLine line(VUB::Faults{});
	Requester near(line.a);
	Responder far(line.b);
	Establish(line, near, far);
	RequesterHandle handle = near.Requests();

	parkRuns = true;
	for (uint16 amount = 8; amount < 10; amount++) {
		Result<VUB::RequestId> sent = SendRunOnce(handle, amount);
		SIM_EXPECT(sent.IsErr() == false);
	}

	bool parked = RunUntil(line, near, far, 100, MaxRounds, []() { return ParkedRunCount() == 2; });
	if (parked == false) {
		StopOnHang("the responder did not park the two requests");
	}

	std::thread canceller([handle]() mutable {
		(void)handle.CancelAll();
	});
	canceller.join();

	parkRuns = false;
	Result<VUB::RequestId> after = SendRunOnce(handle, 12);
	SIM_EXPECT(after.IsErr() == false);

	bool done = RunUntil(line, near, far, 100, MaxRounds, []() { return finalsTotal >= 3; });
	RunRounds(line, near, far, 100, 5);
	SIM_EXPECT(done);
	SIM_EXPECT(finalsTotal == 3);
	SIM_EXPECT(finalsNotOk == 2);
	SIM_EXPECT(cancelHandlerCalls == 2);
	SIM_EXPECT(closedByKey[12] == 1);
	SIM_EXPECT(closedByKey[8] == 0);
	SIM_EXPECT(closedByKey[9] == 0);
}


// The responder emits twenty events from another thread. Each event reaches the requester's handler once. The emitter sends event number
// count only after the main thread has let it (emitGate), and each event is delivered before the next is sent, so the requester's receive
// queue never has to hold more than one of them.
void TestEmitFromAnotherThread() {
	ResetCounters();
	ReportingPacketLine line(VUB::Faults{});
	Requester near(line.a);
	Responder far(line.b);
	Establish(line, near, far);
	ResponderHandle farHandle = far.Requests();

	std::atomic<uint32> emitGate{0};
	std::atomic<uint32> emitted{0};
	std::thread emitter([farHandle, &emitGate, &emitted]() mutable {
		for (uint32 count = 0; count < 20; count++) {
			uint32 spins = 0;
			while (emitGate.load() <= count && spins < MaxSpins) {
				spins++;
				std::this_thread::yield();
			}

			if (emitGate.load() <= count) {
				return;
			}

			Job::JobTick tick{};
			tick.count = count;
			for (uint32 attempt = 0; attempt < MaxSendAttempts; attempt++) {
				if (farHandle.Emit<Job::Event::tick>(tick) == ResultStatus::ok) {
					emitted++;
					break;
				}

				std::this_thread::yield();
			}
		}
	});

	bool done = true;
	for (uint32 count = 0; count < 20 && done; count++) {
		emitGate.store(count + 1);
		done = RunUntil(line, near, far, 100, MaxRounds, [count]() { return tickCount > count; });
	}

	emitter.join();
	RunRounds(line, near, far, 100, 5);
	SIM_EXPECT(done);
	SIM_EXPECT(emitted.load() == 20);
	SIM_EXPECT(tickCount == 20);
	for (uint32 count = 0; count < 20; count++) {
		SIM_EXPECT(ticksSeen[count] == 1);
	}
}


// The table of four slots is full: a fifth send is refused busy and nothing is sent for it. After the parked requests are finished, a
// thread sends six more with retries. Every accepted request is closed once; the refused one is never closed.
void TestFullTableIsBusyAndLosesNothing() {
	ResetCounters();
	ReportingPacketLine line(VUB::Faults{});
	Requester near(line.a);
	Responder far(line.b);
	Establish(line, near, far);
	RequesterHandle handle = near.Requests();

	parkRuns = true;
	for (uint16 amount = 0; amount < 4; amount++) {
		Result<VUB::RequestId> sent = SendRunOnce(handle, amount);
		SIM_EXPECT(sent.IsErr() == false);
	}

	bool parked = RunUntil(line, near, far, 100, MaxRounds, []() { return ParkedRunCount() == 4; });
	if (parked == false) {
		StopOnHang("the responder did not park the four requests");
	}

	Result<VUB::RequestId> refused = SendRunOnce(handle, 4);
	SIM_EXPECT(refused.IsErr());
	SIM_EXPECT(refused.IsErr() && refused.Error() == ResultStatus::busy);
	RunRounds(line, near, far, 100, 3);
	SIM_EXPECT(finalsTotal == 0);

	parkRuns = false;
	std::vector<ParkedRun> held;
	{
		std::lock_guard<std::mutex> guard(parkMutex);
		held.swap(parkedRuns);
	}

	for (ParkedRun& request : held) {
		Job::JobReport report{};
		report.done = request.amount;
		SIM_EXPECT(request.ticket.Finish(Job::JobStatus::started, report) == ResultStatus::ok);
	}

	bool released = RunUntil(line, near, far, 100, MaxRounds, []() { return finalsTotal >= 4; });
	if (released == false) {
		StopOnHang("the four parked requests did not close after their tickets finished");
	}

	std::atomic<uint32> sendFailures{0};
	std::thread sender([handle, &sendFailures]() {
		for (uint16 amount = 10; amount < 16; amount++) {
			if (SendRunUntilAccepted(handle, amount) == false) {
				sendFailures++;
			}
		}
	});

	bool done = RunUntil(line, near, far, 100, MaxRounds, []() { return finalsTotal >= 10; });
	sender.join();
	RunRounds(line, near, far, 100, 5);
	SIM_EXPECT(done);
	SIM_EXPECT(sendFailures.load() == 0);
	SIM_EXPECT(finalsTotal == 10);
	SIM_EXPECT(finalsNotOk == 0);
	for (uint16 amount = 0; amount < 4; amount++) {
		SIM_EXPECT(closedByKey[amount] == 1);
	}

	SIM_EXPECT(closedByKey[4] == 0);
	for (uint16 amount = 10; amount < 16; amount++) {
		SIM_EXPECT(closedByKey[amount] == 1);
	}
}


// A thread awaits Command::run. The responder sends an interim and then the final at once; Await returns the final, and the interim handler
// runs on the main thread.
void TestAwaitAnsweredWithInterim() {
	ResetCounters();
	ReportingPacketLine line(VUB::Faults{});
	Requester near(line.a);
	Responder far(line.b);
	Establish(line, near, far);
	RequesterHandle handle = near.Requests();

	interimFirst = true;
	VUB::Final<Job::JobStatus, Job::JobReport> awaited{};
	std::atomic<bool> returned{false};
	std::thread waiter([handle, &awaited, &returned]() {
		awaited = AwaitRun(handle, 40);
		returned.store(true);
	});

	bool done = RunUntil(line, near, far, 100, MaxRounds, [&returned]() { return returned.load(); });
	if (done == false) {
		StopOnHang("Await did not return the final of an answered request");
	}

	waiter.join();
	SIM_EXPECT(awaited.result == ResultStatus::ok);
	SIM_EXPECT(awaited.status == Job::JobStatus::started);
	SIM_EXPECT(awaited.body.done == 40);
	SIM_EXPECT(interimsTotal == 1);
	SIM_EXPECT(finalsTotal == 0);
}


// The responder has no free ticket: three Command::hold requests are parked, and a fourth is refused busy by the responder. Await returns busy.
// After the parked requests are finished they close with ok.
void TestAwaitRefusedWhenResponderIsFull() {
	ResetCounters();
	ReportingPacketLine line(VUB::Faults{});
	Requester near(line.a);
	SmallResponder far(line.b);
	Establish(line, near, far);
	RequesterHandle handle = near.Requests();

	parkHolds = true;
	for (uint16 amount = 0; amount < 3; amount++) {
		Result<VUB::RequestId> sent = SendHoldOnce(handle, amount);
		SIM_EXPECT(sent.IsErr() == false);
	}

	bool parked = RunUntil(line, near, far, 100, MaxRounds, []() { return ParkedHoldCount() == 3; });
	if (parked == false) {
		StopOnHang("the responder did not park the three requests");
	}

	VUB::Final<Job::JobStatus, VUB::NoBody> awaited{};
	std::atomic<bool> returned{false};
	std::thread waiter([handle, &awaited, &returned]() {
		awaited = AwaitHold(handle, 9);
		returned.store(true);
	});

	bool done = RunUntil(line, near, far, 100, MaxRounds, [&returned]() { return returned.load(); });
	if (done == false) {
		StopOnHang("Await did not return for a refused request");
	}

	waiter.join();
	SIM_EXPECT(awaited.result == ResultStatus::busy);

	parkHolds = false;
	std::vector<ParkedHold> held;
	{
		std::lock_guard<std::mutex> guard(parkMutex);
		held.swap(parkedHolds);
	}

	for (ParkedHold& request : held) {
		SIM_EXPECT(request.ticket.Finish(Job::JobStatus::started) == ResultStatus::ok);
	}

	bool closed = RunUntil(line, near, far, 100, MaxRounds, []() { return holdFinals >= 3; });
	RunRounds(line, near, far, 100, 5);
	SIM_EXPECT(closed);
	SIM_EXPECT(holdFinals == 3);
	SIM_EXPECT(lastResult == ResultStatus::ok);
}


// The responder never answers Command::hold. Await returns timeout when the request's own deadline (3000 ms) passes.
void TestAwaitTimesOut() {
	ResetCounters();
	ReportingPacketLine line(VUB::Faults{});
	Requester near(line.a);
	Responder far(line.b);
	Establish(line, near, far);
	RequesterHandle handle = near.Requests();

	parkHolds = true;
	VUB::Final<Job::JobStatus, VUB::NoBody> awaited{};
	std::atomic<bool> returned{false};
	std::thread waiter([handle, &awaited, &returned]() {
		awaited = AwaitHold(handle, 5);
		returned.store(true);
	});

	// The request must be sent and parked before the deadline rounds start: the waiting thread may start after the rounds would have run out.
	bool parked = RunUntil(line, near, far, 100, MaxRounds, []() { return ParkedHoldCount() == 1; });
	if (parked == false) {
		StopOnHang("the responder did not park the request of the Await");
	}

	bool done = RunUntil(line, near, far, 100000, 1000, [&returned]() { return returned.load(); });
	if (done == false) {
		StopOnHang("Await did not return at the request's deadline");
	}

	waiter.join();
	SIM_EXPECT(awaited.result == ResultStatus::timeout);
	SIM_EXPECT(holdFinals == 0);
}


// The clock jumps past the limit of an Await (its request's deadline plus 250 ms) before the responder answers. The Await gives up with
// timeout. The request's own deadline then passes at the layer, which frees the slot with no handler run. The answer that comes later is
// dropped and counted in lateReplies; no final handler runs for it. After that the slot is free once: four requests are accepted and
// a fifth is busy.
void TestAwaitAbandonedThenLateAnswerFreesSlotOnce() {
	ResetCounters();
	ReportingPacketLine line(VUB::Faults{});
	Requester near(line.a);
	Responder far(line.b);
	Establish(line, near, far);
	RequesterHandle handle = near.Requests();

	parkHolds = true;
	VUB::Final<Job::JobStatus, VUB::NoBody> awaited{};
	std::atomic<bool> returned{false};
	std::thread waiter([handle, &awaited, &returned]() {
		awaited = AwaitHold(handle, 21);
		returned.store(true);
	});

	bool parked = RunUntil(line, near, far, 100, MaxRounds, []() { return ParkedHoldCount() == 1; });
	if (parked == false) {
		StopOnHang("the responder did not park the request of the Await");
	}

	// The Await's limit is a tick of the system (WaitCondition), so the tick passes it; the stack's clock passes the request's deadline, so the
	// rounds free the slot.
	uint32 savedUs = nowUs.load();
	nowUs.store(savedUs + 10000000);
	uint32 spins = 0;
	while (returned.load() == false && spins < MaxSpins) {
		spins++;
		System::tickLow = System::tickLow + 100u;
		std::this_thread::yield();
	}

	if (returned.load() == false) {
		StopOnHang("Await did not give up at its limit");
	}

	waiter.join();
	SIM_EXPECT(awaited.result == ResultStatus::timeout);

	// The layer's own deadline of the request has passed with the clock; the rounds free its slot. No final handler runs for it.
	RunRounds(line, near, far, 100, 3);

	// The late answer: the parked ticket is finished, and its final goes through the rounds. The slot is already closed, so the final is
	// dropped and counted in lateReplies.
	std::optional<ParkedHold> late = TakeParkedHold();
	if (late.has_value() == false) {
		StopOnHang("the parked request is missing");
	}

	SIM_EXPECT(late->ticket.Finish(Job::JobStatus::started) == ResultStatus::ok);
	RunRounds(line, near, far, 100, 10);
	SIM_EXPECT(handle.LateReplies() == 1);
	SIM_EXPECT(holdFinals == 0);
	SIM_EXPECT(holdHandlerCalls == 1);

	// The slot is free once: four sends are accepted, with a process between them, and a fifth is busy while the four are open.
	parkRuns = false;
	for (uint16 amount = 30; amount < 34; amount++) {
		Result<VUB::RequestId> sent = SendRunOnce(handle, amount);
		SIM_EXPECT(sent.IsErr() == false);
		near.Process();
	}

	Result<VUB::RequestId> extra = SendRunOnce(handle, 34);
	SIM_EXPECT(extra.IsErr());
	SIM_EXPECT(extra.IsErr() && extra.Error() == ResultStatus::busy);
	bool closed = RunUntil(line, near, far, 100, MaxRounds, []() { return finalsTotal >= 4; });
	RunRounds(line, near, far, 100, 5);
	SIM_EXPECT(closed);
	SIM_EXPECT(finalsTotal == 4);
	SIM_EXPECT(finalsNotOk == 0);
	for (uint16 amount = 30; amount < 34; amount++) {
		SIM_EXPECT(closedByKey[amount] == 1);
	}
}


// Three threads await Command::run at once. Each gets its own final, with the amount it sent.
void TestSeveralAwaitsAtOnce() {
	ResetCounters();
	ReportingPacketLine line(VUB::Faults{});
	Requester near(line.a);
	Responder far(line.b);
	Establish(line, near, far);
	RequesterHandle handle = near.Requests();

	std::array<VUB::Final<Job::JobStatus, Job::JobReport>, 3> awaited{};
	std::array<std::atomic<bool>, 3> returned{};
	std::vector<std::thread> waiters;
	for (std::size_t index = 0; index < 3; index++) {
		waiters.emplace_back([handle, index, &awaited, &returned]() {
			awaited[index] = AwaitRun(handle, static_cast<uint16>(50 + index));
			returned[index].store(true);
		});
	}

	bool done = RunUntil(line, near, far, 100, MaxRounds, [&returned]() {
		return returned[0].load() && returned[1].load() && returned[2].load();
	});
	if (done == false) {
		StopOnHang("not every Await returned");
	}

	for (std::thread& waiter : waiters) {
		waiter.join();
	}

	for (std::size_t index = 0; index < 3; index++) {
		uint16 expected = static_cast<uint16>(50 + index);
		SIM_EXPECT(awaited[index].result == ResultStatus::ok);
		SIM_EXPECT(awaited[index].body.done == expected);
	}
}


// The responder restarts while an Await is waiting for a parked Command::hold. The replacement responder has a different boot value; its first sends make the
// requester see a new boot, and the restart closes the Await's request with reset. Only the Await's result is asserted here.
void TestAwaitResultIsResetWhenPeerRestarts() {
	ResetCounters();
	ReportingPacketLine line(VUB::Faults{});
	Requester near(line.a);
	std::optional<Responder> far;
	far.emplace(line.b);
	Establish(line, near, *far);
	RequesterHandle handle = near.Requests();

	parkHolds = true;
	VUB::Final<Job::JobStatus, VUB::NoBody> awaited{};
	std::atomic<bool> returned{false};
	std::thread waiter([handle, &awaited, &returned]() {
		awaited = AwaitHold(handle, 60);
		returned.store(true);
	});

	bool parked = RunUntil(line, near, *far, 100, MaxRounds, []() { return ParkedHoldCount() == 1; });
	if (parked == false) {
		StopOnHang("the responder did not park the request of the Await");
	}

	{
		std::lock_guard<std::mutex> guard(parkMutex);
		parkedHolds.clear();
	}

	far.reset();
	far.emplace(line.b);
	GiveSession(*far, RestartedSession);
	parkHolds = false;
	Result<VUB::RequestId> sent = SendRunOnce(handle, 61);
	SIM_EXPECT(sent.IsErr() == false);

	bool done = RunUntil(line, near, *far, 100, MaxRounds, [&returned]() { return returned.load(); });
	if (done == false) {
		StopOnHang("Await did not return after the peer restarted");
	}

	waiter.join();
	SIM_EXPECT(awaited.result == ResultStatus::reset);
}


// A requester with two packets of queue and eight slots: a third send with no Process between is busy, and a send after Process is accepted.
using QueueRequester = VUB::Stack<+[]() {
	return VUB::Builder()
		.CountDrops()
		.Layer<VUB::Requests<JobCommands, RequesterHandlers>>().Open(8).Pending(2)
		.Layer<VUB::ReliableOf<40, 4>>().Retries(3).TimeoutMs(200).Window(4)
		.Link<ReportingPacketEnd>()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.OnReceive<&ConcurrentReceive>()
		.Clock<&ConcurrentClock>()
		.Concurrent();
}>;


using QueueHandle = decltype(std::declval<QueueRequester&>().Requests());


void TestFullQueueIsBusyUntilProcess() {
	ResetCounters();
	ReportingPacketLine line(VUB::Faults{});
	QueueRequester near(line.a);
	Responder far(line.b);
	GiveSession(near, RequesterSession);
	GiveSession(far, ResponderSession);
	RunRounds(line, near, far, 50, 8);
	QueueHandle handle = near.Requests();

	Job::JobRequest body{};
	body.amount = 0;
	Result<VUB::RequestId> first = handle.Send<Job::Command::run, &OnRunFinal, &OnRunInterim>(body);
	body.amount = 1;
	Result<VUB::RequestId> second = handle.Send<Job::Command::run, &OnRunFinal, &OnRunInterim>(body);
	body.amount = 2;
	Result<VUB::RequestId> third = handle.Send<Job::Command::run, &OnRunFinal, &OnRunInterim>(body);
	SIM_EXPECT(first.IsErr() == false);
	SIM_EXPECT(second.IsErr() == false);
	SIM_EXPECT(third.IsErr() && third.Error() == ResultStatus::busy);

	bool done = RunUntil(line, near, far, 100, MaxRounds, []() { return finalsTotal >= 2; });
	SIM_EXPECT(done);
	body.amount = 3;
	Result<VUB::RequestId> fourth = handle.Send<Job::Command::run, &OnRunFinal, &OnRunInterim>(body);
	SIM_EXPECT(fourth.IsErr() == false);
	done = RunUntil(line, near, far, 100, MaxRounds, []() { return finalsTotal >= 3; });
	RunRounds(line, near, far, 100, 3);
	SIM_EXPECT(done);
	SIM_EXPECT(finalsTotal == 3);
	SIM_EXPECT(closedByKey[0] == 1 && closedByKey[1] == 1 && closedByKey[3] == 1);
	SIM_EXPECT(closedByKey[2] == 0);
}


// A final handler sends from inside Process while a task sends: every request of both closes once, and the echoes are the ones accepted.
void TestHandlerSendsFromProcessWhileTaskSends() {
	ResetCounters();
	ReportingPacketLine line(VUB::Faults{});
	Requester near(line.a);
	Responder far(line.b);
	Establish(line, near, far);
	RequesterHandle handle = near.Requests();
	echoFinals = true;
	echoTarget = &handle;

	std::atomic<bool> producerDone{false};
	std::atomic<uint32> sendFailures{0};
	std::thread producer([handle, &producerDone, &sendFailures]() {
		for (uint16 amount = 0; amount < 40; amount++) {
			if (SendRunUntilAccepted(handle, amount) == false) {
				sendFailures++;
			}
		}

		producerDone.store(true);
	});

	bool done = RunUntil(line, near, far, 100, MaxRounds, [&producerDone]() {
		return producerDone.load() && finalsTotal == 40 + echoSent;
	});
	producer.join();
	RunRounds(line, near, far, 100, 5);
	echoTarget = nullptr;
	echoFinals = false;
	SIM_EXPECT(done);
	SIM_EXPECT(sendFailures.load() == 0);
	SIM_EXPECT(echoSent > 0);
	SIM_EXPECT(finalsTotal == 40 + echoSent);
	SIM_EXPECT(finalsNotOk == 0);
	for (uint16 amount = 0; amount < 40; amount++) {
		SIM_EXPECT(closedByKey[amount] == 1);
	}
}


// A task awaits Command::run. Another task cancels every open request: the responder's cancel handler finishes the ticket, and Await returns
// operationCancelled. No final handler runs for the awaited request.
void TestAwaitCancelledFromAnotherTask() {
	ResetCounters();
	ReportingPacketLine line(VUB::Faults{});
	Requester near(line.a);
	Responder far(line.b);
	Establish(line, near, far);
	RequesterHandle handle = near.Requests();

	parkRuns = true;
	VUB::Final<Job::JobStatus, Job::JobReport> awaited{};
	std::atomic<bool> returned{false};
	std::thread waiter([handle, &awaited, &returned]() {
		awaited = AwaitRun(handle, 70);
		returned.store(true);
	});

	bool parked = RunUntil(line, near, far, 100, MaxRounds, []() { return ParkedRunCount() == 1; });
	if (parked == false) {
		StopOnHang("the responder did not park the awaited request");
	}

	std::thread canceller([handle]() mutable {
		(void)handle.CancelAll();
	});
	canceller.join();

	bool done = RunUntil(line, near, far, 100, MaxRounds, [&returned]() { return returned.load(); });
	if (done == false) {
		StopOnHang("Await did not return after the cancel");
	}

	waiter.join();
	RunRounds(line, near, far, 100, 3);
	SIM_EXPECT(awaited.result == ResultStatus::operationCancelled);
	SIM_EXPECT(cancelHandlerCalls == 1);
	SIM_EXPECT(finalsTotal == 0);
}


// The Await gives up while its request is still open (the stack's clock has not reached the deadline). The final that comes later closes the
// request inside Process with no waiter: it is not a late reply, and no handler runs for it.
void TestAwaitAbandonedFinalBeforeDeadline() {
	ResetCounters();
	ReportingPacketLine line(VUB::Faults{});
	Requester near(line.a);
	Responder far(line.b);
	Establish(line, near, far);
	RequesterHandle handle = near.Requests();

	parkHolds = true;
	VUB::Final<Job::JobStatus, VUB::NoBody> awaited{};
	std::atomic<bool> returned{false};
	std::thread waiter([handle, &awaited, &returned]() {
		awaited = AwaitHold(handle, 22);
		returned.store(true);
	});

	bool parked = RunUntil(line, near, far, 100, MaxRounds, []() { return ParkedHoldCount() == 1; });
	if (parked == false) {
		StopOnHang("the responder did not park the request of the Await");
	}

	// The tick moves while the test waits, as a running system's tick does: the wait's start may be read after an early step.
	uint32 spins = 0;
	while (returned.load() == false && spins < MaxSpins) {
		spins++;
		System::tickLow = System::tickLow + 100u;
		std::this_thread::yield();
	}

	if (returned.load() == false) {
		StopOnHang("Await did not give up at its limit");
	}

	waiter.join();
	SIM_EXPECT(awaited.result == ResultStatus::timeout);

	std::optional<ParkedHold> late = TakeParkedHold();
	if (late.has_value() == false) {
		StopOnHang("the parked request is missing");
	}

	SIM_EXPECT(late->ticket.Finish(Job::JobStatus::started) == ResultStatus::ok);
	RunRounds(line, near, far, 100, 10);
	SIM_EXPECT(holdFinals == 0);
	SIM_EXPECT(holdHandlerCalls == 1);
	SIM_EXPECT(handle.LateReplies() == 0);
}


// The window between the wait's last check and its abandon, forced through the test's own critical section and the RTOS handle. The RTOS
// handle runs in the waiting task's Pause, after its last check and before the next one: it arms the window and moves the tick past the
// limit. The waiting task then makes two section entries: the predicate's check (the first) and the abandon's (the second). The section hook
// lets the other task take the final before the abandon's lock is taken, so the abandon finds the final written and the Await returns it.
bool WindowPause(uint32) {
	if (isWaiterThread && windowPauseArm.load()) {
		windowPauseArm.store(false);
		System::tickLow = System::tickLow + 3250u;
		windowSections.store(0);
		windowArm.store(true);
	}

	return true;
}


void TestAwaitFinalBetweenTheCheckAndTheAbandon() {
	ResetCounters();
	windowPauseArm.store(false);
	windowArm.store(false);
	windowGate.store(false);
	windowDone.store(false);
	windowSections.store(0);
	ReportingPacketLine line(VUB::Faults{});
	Requester near(line.a);
	Responder far(line.b);
	Establish(line, near, far);
	RequesterHandle handle = near.Requests();

	parkHolds = true;
	VUB::Final<Job::JobStatus, VUB::NoBody> awaited{};
	System::rtosDelayMsHandle = WindowPause;
	std::thread waiter([handle, &awaited]() {
		isWaiterThread = true;
		Job::JobRequest body{};
		body.amount = 33;
		awaited = handle.Await<Job::Command::hold>(body);
	});

	bool parked = RunUntil(line, near, far, 100, MaxRounds, []() { return ParkedHoldCount() == 1; });
	if (parked == false) {
		StopOnHang("the responder did not park the request of the Await");
	}

	windowPauseArm.store(true);
	uint32 spins = 0;
	while (windowGate.load() == false && spins < MaxSpins) {
		spins++;
		std::this_thread::yield();
	}

	if (windowGate.load() == false) {
		StopOnHang("the Await did not reach the abandon");
	}

	std::optional<ParkedHold> held = TakeParkedHold();
	if (held.has_value() == false) {
		StopOnHang("the parked request is missing");
	}

	SIM_EXPECT(held->ticket.Finish(Job::JobStatus::started) == ResultStatus::ok);
	RunRounds(line, near, far, 100, 10);
	windowDone.store(true);
	waiter.join();
	System::rtosDelayMsHandle = nullptr;
	SIM_EXPECT(awaited.result == ResultStatus::ok);
	SIM_EXPECT(awaited.status == Job::JobStatus::started);
	SIM_EXPECT(holdFinals == 0);
	SIM_EXPECT(handle.LateReplies() == 0);
}


// Await of Command::note, which has no reply: its result is the delivery's, and the responder's handler runs once.
void TestAwaitOfNoReplyCommand() {
	ResetCounters();
	ReportingPacketLine line(VUB::Faults{});
	Requester near(line.a);
	Responder far(line.b);
	Establish(line, near, far);
	RequesterHandle handle = near.Requests();

	ResultStatus delivered = ResultStatus::busy;
	std::atomic<bool> returned{false};
	std::thread waiter([handle, &delivered, &returned]() {
		Job::JobRequest body{};
		body.amount = 90;
		delivered = handle.Await<Job::Command::note>(body);
		returned.store(true);
	});

	bool done = RunUntil(line, near, far, 100, MaxRounds, [&returned]() { return returned.load(); });
	if (done == false) {
		StopOnHang("Await of a command with no reply did not return");
	}

	waiter.join();
	RunRounds(line, near, far, 100, 3);
	SIM_EXPECT(delivered == ResultStatus::ok);
	SIM_EXPECT(noteHandlerCalls == 1);
}


int main() {
	TestTwoSendersWhileRoundsRun();
	TestFullQueueIsBusyUntilProcess();
	TestHandlerSendsFromProcessWhileTaskSends();
	TestAwaitCancelledFromAnotherTask();
	TestAwaitAbandonedFinalBeforeDeadline();
	TestAwaitFinalBetweenTheCheckAndTheAbandon();
	TestAwaitOfNoReplyCommand();
	TestTicketsFinishedFromAnotherThread();
	TestCancelFromAnotherThread();
	TestCancelAllFromAnotherThread();
	TestEmitFromAnotherThread();
	TestFullTableIsBusyAndLosesNothing();
	TestAwaitAnsweredWithInterim();
	TestAwaitRefusedWhenResponderIsFull();
	TestAwaitTimesOut();
	TestAwaitAbandonedThenLateAnswerFreesSlotOnce();
	TestSeveralAwaitsAtOnce();
	TestAwaitResultIsResetWhenPeerRestarts();
	SIM_EXPECT(handlerDepthErrors == 0);
	SIM_EXPECT(sectionEnters.load() > 0);
	return SimFinish("TestVubRequestsConcurrent");
}
