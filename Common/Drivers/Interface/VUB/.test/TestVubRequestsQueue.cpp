#include <Drivers/Interface/VUB/Bytes.h>
#include <Drivers/Interface/VUB/LinkTraits.h>
#include <Drivers/Interface/VUB/Meta.h>
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
#include <functional>
#include <mutex>
#include <optional>
#include <span>
#include <thread>
#include <vector>
#include "SimCheck.h"


namespace RequestWire = VUB::RequestWire;

// The outgoing queue of a concurrent requests layer: what it holds for Process, and what reaches the wire. The link can be blocked, so the
// stack's control queue (one packet here) stays full and every packet of the layer waits in the layer's queue. Each test follows one packet
// from the call to the wire, or to its removal.


// The critical sections of this program: a recursive mutex, so the sections really exclude each other, as in TestVubRequestsConcurrent.cpp.
std::recursive_mutex sectionMutex;


void System::CriticalSection(bool isEnable) {
	if (isEnable) {
		sectionMutex.lock();
	} else {
		sectionMutex.unlock();
	}
}


// The tick and the RTOS delay handle (Await waits on them). The tick moves only where a test moves it.
inline volatile uint32 System::tickLow = 0;
inline std::function<bool(uint32 delay)> System::rtosDelayMsHandle = nullptr;


inline std::atomic<uint32> nowUs{0};


inline uint32 QueueClock() {
	return nowUs.load();
}


inline void QueueReceive(const VUB::Meta&, std::span<const uint8>) {
}


// A link that takes every packet it is given, unless it is blocked: then it refuses each one busy, and the stack keeps it. wire is the packets
// the link took, in order.
struct QueueLink {
	static constexpr VUB::LinkTraits traits = VUB::LinkTraits().Framed().MaxPacket(512).FullDuplex();
	bool blocked = false;
	std::vector<std::vector<uint8>> wire;


	ResultStatus Send(std::span<const uint8> packet) {
		if (blocked) {
			return ResultStatus::busy;
		}

		wire.emplace_back(packet.begin(), packet.end());
		return ResultStatus::ok;
	}
};


namespace Job {

enum class Command : uint8 {
	run = 0x21,
	note = 0x23,
	bulk = 0x24
};


enum class Event : uint8 {
	tick = 0x40
};


struct JobRequest {
	uint16 amount = 0;
};


struct JobReport {
	uint16 done = 0;
};


struct JobTick {
	uint32 count = 0;
};


// A body of 300 bytes: a packet of more than 255 bytes with its header.
struct BulkBody {
	std::array<uint8, 300> data{};
};


enum class JobStatus : uint8 {
	started = 0
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


template<>
struct BodyOf<Job::BulkBody> {
	static consteval auto Layout() {
		return Fields<Job::BulkBody>()
			.Field<&Job::BulkBody::data>();
	}
};

}


using QueueCommands = VUB::CommandSet<+[]() {
	return VUB::Commands()
		.Command<Job::Command::run>()
			.Request<Job::JobRequest>()
			.Reply<Job::JobStatus, Job::JobReport>()
			.Cancellable()
			.ReplyTimeoutMs(500)
			.TicketTimeoutMs(500)
		.Command<Job::Command::note>()
			.Request<Job::JobRequest>()
			.Fire()
		.Command<Job::Command::bulk>()
			.Request<Job::BulkBody>()
			.Fire()
		.Event<Job::Event::tick>()
			.Body<Job::JobTick>();
}>;


// The requester: sixteen slots, a queue of eight packets, one control packet in the stack, a 400-byte payload, and the ping.
using QueueStack = VUB::Stack<+[]() {
	return VUB::Builder()
		.CountDrops()
		.Layer<VUB::Requests<QueueCommands, VUB::Handlers<>>>().Open(16).Pending(8).Ping(0).Health().BuiltInTimeoutMs(500).Counted().Awaitable()
		.Link<QueueLink>()
		.MaxPayload(400)
		.SendQueue(1)
		.OnReceive<&QueueReceive>()
		.Clock<&QueueClock>()
		.Concurrent();
}>;


using QueueHandle = decltype(std::declval<QueueStack&>().Requests());


// A link that reports its state: while it is down, a stack with WhileDown(Fail) refuses the layer's packets with noConnection, and the
// packet of the layer is not sent. SetUp changes the state through the sink the stack gave.
struct QueueFailLink {
	static constexpr VUB::LinkTraits traits = VUB::LinkTraits().Framed().MaxPacket(512).FullDuplex().ReportsState();
	VUB::StateSink sink;
	std::vector<std::vector<uint8>> wire;


	void SetStateSink(VUB::StateSink value) {
		sink = value;
	}


	void SetUp(bool up) {
		sink.Report(up);
	}


	ResultStatus Send(std::span<const uint8> packet) {
		wire.emplace_back(packet.begin(), packet.end());
		return ResultStatus::ok;
	}
};


using QueueFailStack = VUB::Stack<+[]() {
	return VUB::Builder()
		.Layer<VUB::Requests<QueueCommands, VUB::Handlers<>>>().Open(16).Pending(8).Ping(0).Health().BuiltInTimeoutMs(500).Counted().Awaitable()
		.Link<QueueFailLink>()
		.MaxPayload(400)
		.SendQueue(1)
		.WhileDown(VUB::Down::Fail)
		.OnReceive<&QueueReceive>()
		.Clock<&QueueClock>()
		.Concurrent();
}>;


// The final and the ping handlers of the tests: they count what they get.
inline uint32 finalsSeen = 0;
inline uint32 timeoutsSeen = 0;
inline uint32 pingsSeen = 0;
inline uint32 pingTimeouts = 0;


void OnFinal(const VUB::Final<Job::JobStatus, Job::JobReport>& reply) {
	finalsSeen++;
	if (reply.result == ResultStatus::timeout || reply.result == ResultStatus::operationCancelled) {
		timeoutsSeen++;
	}
}


void OnPing(ResultStatus result, uint32) {
	pingsSeen++;
	if (result == ResultStatus::timeout) {
		pingTimeouts++;
	}
}


inline uint32 healthSeen = 0;
inline uint32 healthTimeouts = 0;


void OnHealth(const VUB::HealthReport<VUB::NoBody>& report) {
	healthSeen++;
	if (report.result == ResultStatus::timeout) {
		healthTimeouts++;
	}
}


void ResetCounters() {
	nowUs.store(1000000);
	finalsSeen = 0;
	timeoutsSeen = 0;
	pingsSeen = 0;
	pingTimeouts = 0;
	healthSeen = 0;
	healthTimeouts = 0;
}


// Runs one Process after the clock moves by stepUs.
template<typename StackType>
void Tick(StackType& stack, uint32 stepUs) {
	nowUs.fetch_add(stepUs);
	stack.Process();
}


// The requests of the wire: a REQUEST packet has its kind, its command id, its number and its tag.
bool IsRequestOf(const std::vector<uint8>& packet, uint8 kind, uint8 id, uint16 number) {
	return packet.size() >= RequestWire::RequestHeader && packet[0] == kind && packet[1] == id && VUB::GetLe16(packet.data() + 2) == number;
}


std::size_t CountRequestsOf(const std::vector<std::vector<uint8>>& wire, uint8 id, uint16 number) {
	std::size_t count = 0;
	for (const std::vector<uint8>& packet : wire) {
		if (IsRequestOf(packet, RequestWire::KindRequest, id, number)) {
			count++;
		}
	}

	return count;
}


// The CANCEL packets of the wire for a number: a CANCEL has its kind, its number and its tag.
std::size_t CountCancelsOf(const std::vector<std::vector<uint8>>& wire, uint16 number) {
	std::size_t count = 0;
	for (const std::vector<uint8>& packet : wire) {
		if (packet.size() >= RequestWire::CancelSize && packet[0] == RequestWire::KindCancel && VUB::GetLe16(packet.data() + 1) == number) {
			count++;
		}
	}

	return count;
}


std::size_t CountRequestsWithId(const std::vector<std::vector<uint8>>& wire, uint8 id) {
	std::size_t count = 0;
	for (const std::vector<uint8>& packet : wire) {
		if (packet.size() >= RequestWire::RequestHeader && packet[0] == RequestWire::KindRequest && packet[1] == id) {
			count++;
		}
	}

	return count;
}


std::size_t CountPackets(const std::vector<std::vector<uint8>>& wire, uint8 kind) {
	std::size_t count = 0;
	for (const std::vector<uint8>& packet : wire) {
		if (!packet.empty() && packet[0] == kind) {
			count++;
		}
	}

	return count;
}


// The control packet the test keeps in the stack while the link is blocked: a fire, which has no slot and no deadline.
void FillControlQueue(QueueStack& stack, QueueHandle& handle, QueueLink& link) {
	link.blocked = true;
	Job::JobRequest body{};
	body.amount = 99;
	SIM_EXPECT(handle.Fire<Job::Command::note>(body) == ResultStatus::ok);
	Tick(stack, 1000);
	SIM_EXPECT(link.wire.empty());
}


// The requests sent after the test, on a link that accepts again: the next request goes out, and nothing of the removed one does.
void Release(QueueStack& stack, QueueLink& link) {
	link.blocked = false;
	Tick(stack, 1000);
	Tick(stack, 1000);
	Tick(stack, 1000);
}


// A request whose deadline passes while the layer's queue holds it (the link refuses it all along) is not sent after the deadline.
void TestClosedRequestLeavesTheQueue() {
	ResetCounters();
	QueueLink link;
	QueueStack stack(link);
	QueueHandle handle = stack.Requests();
	handle.SetSession(1);
	FillControlQueue(stack, handle, link);

	Job::JobRequest body{};
	body.amount = 1;
	Result<VUB::RequestId> doomed = handle.Send<Job::Command::run, &OnFinal>(body);
	SIM_EXPECT(doomed.IsErr() == false);
	Tick(stack, 1000);
	for (uint32 step = 0; step < 8; step++) {
		Tick(stack, 100000);
	}

	SIM_EXPECT(timeoutsSeen == 1);
	body.amount = 2;
	Result<VUB::RequestId> next = handle.Send<Job::Command::run, &OnFinal>(body);
	SIM_EXPECT(next.IsErr() == false);
	Release(stack, link);
	SIM_EXPECT(doomed.IsErr() == false && CountRequestsOf(link.wire, static_cast<uint8>(Job::Command::run), doomed.Value().number) == 0);
	SIM_EXPECT(next.IsErr() == false && CountRequestsOf(link.wire, static_cast<uint8>(Job::Command::run), next.Value().number) == 1);
}


// A cancelled request whose deadline passes while it waits: neither its REQUEST nor its CANCEL goes out after the deadline.
void TestCancelledRequestLeavesTheQueue() {
	ResetCounters();
	QueueLink link;
	QueueStack stack(link);
	QueueHandle handle = stack.Requests();
	handle.SetSession(1);
	FillControlQueue(stack, handle, link);

	Job::JobRequest body{};
	body.amount = 3;
	Result<VUB::RequestId> cancelled = handle.Send<Job::Command::run, &OnFinal>(body);
	SIM_EXPECT(cancelled.IsErr() == false);
	Tick(stack, 1000);
	SIM_EXPECT(handle.Cancel<Job::Command::run>(cancelled.Value()) == ResultStatus::ok);
	for (uint32 step = 0; step < 8; step++) {
		Tick(stack, 100000);
	}

	SIM_EXPECT(timeoutsSeen == 1);
	body.amount = 4;
	Result<VUB::RequestId> next = handle.Send<Job::Command::run, &OnFinal>(body);
	SIM_EXPECT(next.IsErr() == false);
	Release(stack, link);
	const uint16 number = cancelled.Value().number;
	SIM_EXPECT(CountRequestsOf(link.wire, static_cast<uint8>(Job::Command::run), number) == 0);
	SIM_EXPECT(CountCancelsOf(link.wire, number) == 0);
	SIM_EXPECT(next.IsErr() == false && CountRequestsOf(link.wire, static_cast<uint8>(Job::Command::run), next.Value().number) == 1);
}


// An Await whose wait gave up (its limit passed) and whose deadline then passed: its REQUEST does not go out later. The waiter's task runs
// on its own thread; the main thread moves the clock and runs the stack.
void TestAbandonedAwaitLeavesTheQueue() {
	ResetCounters();
	QueueLink link;
	QueueStack stack(link);
	QueueHandle handle = stack.Requests();
	handle.SetSession(1);
	FillControlQueue(stack, handle, link);

	std::atomic<bool> returned{false};
	VUB::Final<Job::JobStatus, Job::JobReport> awaited{};
	std::thread waiter([handle, &awaited, &returned]() mutable {
		Job::JobRequest body{};
		body.amount = 5;
		awaited = handle.Await<Job::Command::run>(body);
		returned.store(true);
	});

	uint32 spins = 0;
	while (returned.load() == false && spins < 2000000) {
		spins++;
		System::tickLow = System::tickLow + 100u;
		std::this_thread::yield();
	}

	waiter.join();
	SIM_EXPECT(returned.load());
	SIM_EXPECT(awaited.result == ResultStatus::timeout);
	Tick(stack, 1000);
	for (uint32 step = 0; step < 8; step++) {
		Tick(stack, 100000);
	}

	Job::JobRequest body{};
	body.amount = 6;
	Result<VUB::RequestId> next = handle.Send<Job::Command::run, &OnFinal>(body);
	SIM_EXPECT(next.IsErr() == false);
	Release(stack, link);
	SIM_EXPECT(CountPackets(link.wire, RequestWire::KindRequest) == 2);
	SIM_EXPECT(CountRequestsWithId(link.wire, static_cast<uint8>(Job::Command::run)) == 1);
	SIM_EXPECT(next.IsErr() == false && CountRequestsOf(link.wire, static_cast<uint8>(Job::Command::run), next.Value().number) == 1);
}


// A ping whose deadline passes while the queue holds it: the ping does not go out later, and its handler got the timeout.
void TestPingLeavesTheQueue() {
	ResetCounters();
	QueueLink link;
	QueueStack stack(link);
	QueueHandle handle = stack.Requests();
	handle.SetSession(1);
	FillControlQueue(stack, handle, link);

	SIM_EXPECT(handle.Ping<&OnPing>(0) == ResultStatus::ok);
	Tick(stack, 1000);
	for (uint32 step = 0; step < 8; step++) {
		Tick(stack, 100000);
	}

	SIM_EXPECT(pingsSeen == 1 && pingTimeouts == 1);
	Job::JobRequest body{};
	body.amount = 7;
	Result<VUB::RequestId> next = handle.Send<Job::Command::run, &OnFinal>(body);
	SIM_EXPECT(next.IsErr() == false);
	Release(stack, link);
	SIM_EXPECT(CountRequestsWithId(link.wire, VUB::BuiltInWire::IdPing) == 0);
	SIM_EXPECT(CountPackets(link.wire, RequestWire::KindRequest) == 2);
	SIM_EXPECT(next.IsErr() == false && CountRequestsOf(link.wire, static_cast<uint8>(Job::Command::run), next.Value().number) == 1);
}


// The layer's queue full of fires, with the link blocked: the stack's control packet takes one fire, and eight more wait in the layer's queue,
// so a ping or a health request sent now finds no room in the queue.
void FillLayerQueue(QueueStack& stack, QueueHandle& handle, QueueLink& link) {
	FillControlQueue(stack, handle, link);
	Job::JobRequest body{};
	body.amount = 98;
	for (uint32 index = 0; index < 8; index++) {
		SIM_EXPECT(handle.Fire<Job::Command::note>(body) == ResultStatus::ok);
	}

	Tick(stack, 1000);
	SIM_EXPECT(link.wire.empty());
}


// A cancel that the stack refuses (the link is down under Fail) is held: the next Process sends it once the link is up again. The
// request itself was sent before the link went down, and the cancel is the packet that the refusal hits.
void TestCancelRefusedWhileDownIsSentAgain() {
	ResetCounters();
	QueueFailLink link;
	QueueFailStack stack(link);
	link.SetUp(true);
	decltype(stack.Requests()) handle = stack.Requests();
	handle.SetSession(1);
	Job::JobRequest body{};
	body.amount = 9;
	Result<VUB::RequestId> sent = handle.Send<Job::Command::run, &OnFinal>(body);
	SIM_EXPECT(sent.IsErr() == false);
	const uint16 number = sent.Value().number;
	Tick(stack, 1000);
	SIM_EXPECT(CountRequestsOf(link.wire, static_cast<uint8>(Job::Command::run), number) == 1);

	link.SetUp(false);
	SIM_EXPECT(handle.Cancel<Job::Command::run>(sent.Value()) == ResultStatus::ok);
	Tick(stack, 1000);
	SIM_EXPECT(CountCancelsOf(link.wire, number) == 0);

	link.SetUp(true);
	Tick(stack, 1000);
	SIM_EXPECT(CountCancelsOf(link.wire, number) == 1);
}


// A ping that the full queue refuses stays queued: its BuiltInTimeoutMs runs from the first Process after the call, so it ends with timeout,
// and nothing of it goes out after the link returns.
void TestPingRefusedByFullQueueTimesOut() {
	ResetCounters();
	QueueLink link;
	QueueStack stack(link);
	QueueHandle handle = stack.Requests();
	handle.SetSession(1);
	FillLayerQueue(stack, handle, link);

	SIM_EXPECT(handle.Ping<&OnPing>(0) == ResultStatus::ok);
	Tick(stack, 1000);
	for (uint32 step = 0; step < 8; step++) {
		Tick(stack, 100000);
	}

	SIM_EXPECT(pingsSeen == 1 && pingTimeouts == 1);
	Release(stack, link);
	SIM_EXPECT(CountRequestsWithId(link.wire, VUB::BuiltInWire::IdPing) == 0);
}


// A health request that the full queue refuses ends with timeout at BuiltInTimeoutMs after the first Process, and is not sent later.
void TestHealthRefusedByFullQueueTimesOut() {
	ResetCounters();
	QueueLink link;
	QueueStack stack(link);
	QueueHandle handle = stack.Requests();
	handle.SetSession(1);
	FillLayerQueue(stack, handle, link);

	SIM_EXPECT(handle.Health<&OnHealth>() == ResultStatus::ok);
	Tick(stack, 1000);
	for (uint32 step = 0; step < 8; step++) {
		Tick(stack, 100000);
	}

	SIM_EXPECT(healthSeen == 1 && healthTimeouts == 1);
	Release(stack, link);
	SIM_EXPECT(CountRequestsWithId(link.wire, VUB::BuiltInWire::IdHealth) == 0);
}


// A request of more than 255 bytes: the queue keeps its whole packet, and the wire has the same bytes.
void TestPacketLongerThan255BytesIsSentWhole() {
	ResetCounters();
	QueueLink link;
	QueueStack stack(link);
	QueueHandle handle = stack.Requests();
	handle.SetSession(1);

	Job::BulkBody body{};
	for (std::size_t index = 0; index < body.data.size(); index++) {
		body.data[index] = static_cast<uint8>(index * 7 + 1);
	}

	SIM_EXPECT(handle.Fire<Job::Command::bulk>(body) == ResultStatus::ok);
	Tick(stack, 1000);
	Tick(stack, 1000);
	SIM_EXPECT(link.wire.size() == 1);
	if (link.wire.size() == 1) {
		const std::vector<uint8>& packet = link.wire[0];
		SIM_EXPECT(packet.size() == RequestWire::RequestHeader + body.data.size());
		bool same = packet.size() == RequestWire::RequestHeader + body.data.size();
		for (std::size_t index = 0; same && index < body.data.size(); index++) {
			same = packet[RequestWire::RequestHeader + index] == body.data[index];
		}

		SIM_EXPECT(same);
	}
}


// The packets a handler of this stack makes run from Process. The request of a handler is answered only from the stack's Process; the
// handler's packets must reach the link in the same Process that ran the handler.
inline uint8 handlerAction = 0;
inline QueueHandle* handlerHandle = nullptr;
inline VUB::RequestId handlerTarget{};
inline uint32 handlerRuns = 0;
inline uint16 handlerSent = 0;


void OnTimeoutDoes(const VUB::Final<Job::JobStatus, Job::JobReport>& reply) {
	OnFinal(reply);
	if (reply.result != ResultStatus::timeout || handlerAction == 0) {
		return;
	}

	handlerRuns++;
	uint8 action = handlerAction;
	handlerAction = 0;
	Job::JobRequest body{};
	body.amount = 50;
	if (action == 1) {
		Result<VUB::RequestId> sent = handlerHandle->Send<Job::Command::run, &OnFinal>(body);
		if (sent.IsErr() == false) {
			handlerSent = sent.Value().number;
		}
	} else if (action == 2) {
		(void)handlerHandle->Fire<Job::Command::note>(body);
	} else if (action == 3) {
		Job::JobTick tick{};
		tick.count = 9;
		(void)handlerHandle->Emit<Job::Event::tick>(tick);
	} else {
		(void)handlerHandle->Cancel<Job::Command::run>(handlerTarget);
	}
}


// Runs the action in a handler of the first request's final, which is a timeout. The packet of the action must be on the link after the same
// Process that ran the handler. Returns nothing; the checks are asserted.
void RunHandlerAction(uint8 action, uint8 kind, uint8 id) {
	ResetCounters();
	handlerSent = 0;
	QueueLink link;
	QueueStack stack(link);
	QueueHandle handle = stack.Requests();
	handle.SetSession(1);
	handlerHandle = &handle;
	handlerRuns = 0;
	handlerAction = 0;

	Job::JobRequest body{};
	body.amount = 5;
	Result<VUB::RequestId> first = handle.Send<Job::Command::run, &OnTimeoutDoes>(body);
	SIM_EXPECT(first.IsErr() == false);
	Tick(stack, 1000);
	if (action == 4) {
		// The request to cancel is sent and armed one step later, so its deadline is after the first request's.
		body.amount = 6;
		Result<VUB::RequestId> second = handle.Send<Job::Command::run, &OnFinal>(body);
		SIM_EXPECT(second.IsErr() == false);
		if (second.IsErr() == false) {
			handlerTarget = second.Value();
		}

		Tick(stack, 100000);
	}

	handlerAction = action;
	std::size_t before = link.wire.size();
	uint32 spins = 0;
	while (handlerRuns == 0 && spins < 20) {
		spins++;
		before = link.wire.size();
		Tick(stack, 100000);
	}

	SIM_EXPECT(handlerRuns == 1);
	// The number of the packet the handler made: a request of action 1, the request to cancel of action 4. A fire has no number the test sees.
	uint16 expectNumber = action == 1 ? handlerSent : handlerTarget.number;
	bool found = false;
	for (std::size_t index = before; index < link.wire.size(); index++) {
		const std::vector<uint8>& packet = link.wire[index];
		if (kind == RequestWire::KindEvent) {
			found = found || (packet.size() >= 2 && packet[0] == kind && packet[1] == id);
		} else if (kind == RequestWire::KindCancel) {
			found = found || (packet.size() >= RequestWire::CancelSize && packet[0] == kind && VUB::GetLe16(packet.data() + 1) == expectNumber);
		} else {
			found = found || (action == 2 ? (packet.size() >= 2 && packet[0] == kind && packet[1] == id) : IsRequestOf(packet, kind, id, expectNumber));
		}
	}

	SIM_EXPECT(found);
	handlerHandle = nullptr;
}


// The four actions of a handler, each from the handler of a timed-out request. The packet is on the wire in the same Process.
void TestRequestMadeInHandlerGoesOutInThatProcess() {
	RunHandlerAction(1, RequestWire::KindRequest, static_cast<uint8>(Job::Command::run));
}


void TestFireMadeInHandlerGoesOutInThatProcess() {
	RunHandlerAction(2, RequestWire::KindRequest, static_cast<uint8>(Job::Command::note));
}


void TestEventMadeInHandlerGoesOutInThatProcess() {
	RunHandlerAction(3, RequestWire::KindEvent, static_cast<uint8>(Job::Event::tick));
}


void TestCancelMadeInHandlerGoesOutInThatProcess() {
	RunHandlerAction(4, RequestWire::KindCancel, 0);
}


// Two tasks: one fires forty packets, the other pings twenty times, while the main thread runs the stack. Every accepted fire is on the wire
// once, every accepted ping is on the wire once, and every ping's handler gets its timeout (the link has no peer here).
void TestConcurrentPingAndFireFromTwoTasks() {
	ResetCounters();
	QueueLink link;
	QueueStack stack(link);
	QueueHandle handle = stack.Requests();
	handle.SetSession(1);

	std::atomic<uint32> firesAccepted{0};
	std::atomic<uint32> pingsAccepted{0};
	std::atomic<uint32> refused{0};
	std::atomic<bool> firesDone{false};
	std::atomic<bool> pingsDone{false};
	std::thread fires([handle, &firesAccepted, &refused, &firesDone]() mutable {
		for (uint32 index = 0; index < 40; index++) {
			Job::JobRequest body{};
			body.amount = static_cast<uint16>(index);
			uint32 attempts = 0;
			ResultStatus status = handle.Fire<Job::Command::note>(body);
			while (status == ResultStatus::busy && attempts < 2000000) {
				attempts++;
				std::this_thread::yield();
				status = handle.Fire<Job::Command::note>(body);
			}

			if (status == ResultStatus::ok) {
				firesAccepted++;
			} else {
				refused++;
			}
		}

		firesDone.store(true);
	});
	std::thread pings([handle, &pingsAccepted, &refused, &pingsDone]() mutable {
		for (uint32 index = 0; index < 20; index++) {
			uint32 attempts = 0;
			ResultStatus status = handle.Ping<&OnPing>(0);
			while (status == ResultStatus::busy && attempts < 2000000) {
				attempts++;
				std::this_thread::yield();
				status = handle.Ping<&OnPing>(0);
			}

			if (status == ResultStatus::ok) {
				pingsAccepted++;
			} else {
				refused++;
			}
		}

		pingsDone.store(true);
	});

	// The stack takes one packet per Process, so the packets of both tasks need 60 of them. A ping's time runs while it waits in the queue: with
	// 1 ms between two Processes each one is sent long before its 500 ms.
	uint32 steps = 0;
	while ((firesDone.load() == false || pingsDone.load() == false) && steps < 2000000) {
		steps++;
		Tick(stack, 1000);
		std::this_thread::yield();
	}

	fires.join();
	pings.join();
	for (uint32 step = 0; step < 1000; step++) {
		Tick(stack, 1000);
	}

	SIM_EXPECT(refused.load() == 0);
	SIM_EXPECT(firesAccepted.load() == 40);
	SIM_EXPECT(pingsAccepted.load() == 20);
	SIM_EXPECT(CountRequestsWithId(link.wire, static_cast<uint8>(Job::Command::note)) == 40);
	SIM_EXPECT(CountRequestsWithId(link.wire, VUB::BuiltInWire::IdPing) == 20);
	SIM_EXPECT(pingsSeen == 20 && pingTimeouts == 20);
}


// One task sends a request and then an event, forty times, and the main thread runs the stack. The REQUEST packets of the task rise on the wire
// in the order of the calls, the events too, and the two kinds alternate on the wire as they were called (the queue is one line).
void TestWireOrderOfOneTask() {
	ResetCounters();
	QueueLink link;
	QueueStack stack(link);
	QueueHandle handle = stack.Requests();
	handle.SetSession(1);

	std::atomic<bool> taskDone{false};
	std::atomic<uint32> refused{0};
	std::thread task([handle, &taskDone, &refused]() mutable {
		for (uint32 index = 0; index < 40; index++) {
			Job::JobRequest body{};
			body.amount = static_cast<uint16>(index);
			uint32 attempts = 0;
			ResultStatus status = ResultStatus::busy;
			while (status == ResultStatus::busy && attempts < 2000000) {
				attempts++;
				Result<VUB::RequestId> sent = handle.Send<Job::Command::run, &OnFinal>(body);
				status = sent.IsErr() ? sent.Error() : ResultStatus::ok;
				if (status == ResultStatus::busy) {
					std::this_thread::yield();
				}
			}

			if (status != ResultStatus::ok) {
				refused++;
			}

			Job::JobTick tick{};
			tick.count = index;
			attempts = 0;
			status = ResultStatus::busy;
			while (status == ResultStatus::busy && attempts < 2000000) {
				attempts++;
				status = handle.Emit<Job::Event::tick>(tick);
				if (status == ResultStatus::busy) {
					std::this_thread::yield();
				}
			}

			if (status != ResultStatus::ok) {
				refused++;
			}
		}

		taskDone.store(true);
	});

	uint32 steps = 0;
	while (taskDone.load() == false && steps < 2000000) {
		steps++;
		Tick(stack, 10000);
		std::this_thread::yield();
	}

	task.join();
	for (uint32 step = 0; step < 100; step++) {
		Tick(stack, 10000);
	}

	// The wire, in order: the kinds of the task's packets, the request numbers, and the event counts.
	std::vector<uint8> kinds;
	std::vector<uint16> numbers;
	std::vector<uint32> counts;
	for (const std::vector<uint8>& packet : link.wire) {
		if (packet.size() >= RequestWire::RequestHeader && packet[0] == RequestWire::KindRequest && packet[1] == static_cast<uint8>(Job::Command::run)) {
			kinds.push_back(RequestWire::KindRequest);
			numbers.push_back(VUB::GetLe16(packet.data() + 2));
		} else if (packet.size() >= RequestWire::EventHeader + 4 && packet[0] == RequestWire::KindEvent) {
			kinds.push_back(RequestWire::KindEvent);
			counts.push_back(VUB::GetLe32(packet.data() + RequestWire::EventHeader));
		}
	}

	bool numbersRise = true;
	for (std::size_t index = 1; index < numbers.size(); index++) {
		numbersRise = numbersRise && numbers[index] > numbers[index - 1];
	}

	bool countsInOrder = true;
	for (std::size_t index = 0; index < counts.size(); index++) {
		countsInOrder = countsInOrder && counts[index] == index;
	}

	bool alternating = kinds.size() == 80;
	for (std::size_t index = 0; alternating && index < kinds.size(); index++) {
		alternating = kinds[index] == ((index % 2 == 0) ? RequestWire::KindRequest : RequestWire::KindEvent);
	}

	SIM_EXPECT(refused.load() == 0);
	SIM_EXPECT(numbers.size() == 40);
	SIM_EXPECT(numbersRise);
	SIM_EXPECT(counts.size() == 40);
	SIM_EXPECT(countsInOrder);
	SIM_EXPECT(alternating);
}


// Two tasks send health requests while the main thread runs the stack. Every accepted health request is on the wire once, and its handler gets
// its timeout (no peer answers here).
void TestConcurrentHealthFromTwoTasks() {
	ResetCounters();
	QueueLink link;
	QueueStack stack(link);
	QueueHandle handle = stack.Requests();
	handle.SetSession(1);

	std::atomic<uint32> accepted{0};
	std::atomic<uint32> refused{0};
	std::atomic<uint32> tasksDone{0};
	std::thread first([handle, &accepted, &refused, &tasksDone]() mutable {
		for (uint32 index = 0; index < 15; index++) {
			uint32 attempts = 0;
			ResultStatus status = handle.Health<&OnHealth>();
			while (status == ResultStatus::busy && attempts < 2000000) {
				attempts++;
				std::this_thread::yield();
				status = handle.Health<&OnHealth>();
			}

			if (status == ResultStatus::ok) {
				accepted++;
			} else {
				refused++;
			}
		}

		tasksDone++;
	});
	std::thread second([handle, &accepted, &refused, &tasksDone]() mutable {
		for (uint32 index = 0; index < 15; index++) {
			uint32 attempts = 0;
			ResultStatus status = handle.Health<&OnHealth>();
			while (status == ResultStatus::busy && attempts < 2000000) {
				attempts++;
				std::this_thread::yield();
				status = handle.Health<&OnHealth>();
			}

			if (status == ResultStatus::ok) {
				accepted++;
			} else {
				refused++;
			}
		}

		tasksDone++;
	});

	uint32 steps = 0;
	while (tasksDone.load() < 2 && steps < 2000000) {
		steps++;
		Tick(stack, 10000);
		std::this_thread::yield();
	}

	first.join();
	second.join();
	for (uint32 step = 0; step < 100; step++) {
		Tick(stack, 10000);
	}

	SIM_EXPECT(refused.load() == 0);
	SIM_EXPECT(accepted.load() == 30);
	SIM_EXPECT(CountRequestsWithId(link.wire, VUB::BuiltInWire::IdHealth) == 30);
	SIM_EXPECT(healthSeen == 30 && healthTimeouts == 30);
}


// A health request whose deadline passes while the queue holds it: it does not go out later, and its handler got the timeout.
void TestClosedHealthLeavesTheQueue() {
	ResetCounters();
	QueueLink link;
	QueueStack stack(link);
	QueueHandle handle = stack.Requests();
	handle.SetSession(1);
	FillControlQueue(stack, handle, link);

	SIM_EXPECT(handle.Health<&OnHealth>() == ResultStatus::ok);
	Tick(stack, 1000);
	for (uint32 step = 0; step < 8; step++) {
		Tick(stack, 100000);
	}

	SIM_EXPECT(healthSeen == 1 && healthTimeouts == 1);
	Job::JobRequest body{};
	body.amount = 8;
	Result<VUB::RequestId> next = handle.Send<Job::Command::run, &OnFinal>(body);
	SIM_EXPECT(next.IsErr() == false);
	Release(stack, link);
	SIM_EXPECT(CountRequestsWithId(link.wire, VUB::BuiltInWire::IdHealth) == 0);
	SIM_EXPECT(CountPackets(link.wire, RequestWire::KindRequest) == 2);
	SIM_EXPECT(next.IsErr() == false && CountRequestsOf(link.wire, static_cast<uint8>(Job::Command::run), next.Value().number) == 1);
}


int main() {
	TestConcurrentHealthFromTwoTasks();
	TestClosedHealthLeavesTheQueue();
	TestConcurrentPingAndFireFromTwoTasks();
	TestWireOrderOfOneTask();
	TestClosedRequestLeavesTheQueue();
	TestCancelledRequestLeavesTheQueue();
	TestAbandonedAwaitLeavesTheQueue();
	TestPingLeavesTheQueue();
	TestCancelRefusedWhileDownIsSentAgain();
	TestPingRefusedByFullQueueTimesOut();
	TestHealthRefusedByFullQueueTimesOut();
	TestPacketLongerThan255BytesIsSentWhole();
	TestRequestMadeInHandlerGoesOutInThatProcess();
	TestFireMadeInHandlerGoesOutInThatProcess();
	TestEventMadeInHandlerGoesOutInThatProcess();
	TestCancelMadeInHandlerGoesOutInThatProcess();
	return SimFinish("TestVubRequestsQueue");
}
