#include "SimCheck.h"
#include <Drivers/Interface/VUB/LinkTraits.h>
#include <Drivers/Interface/VUB/Meta.h>
#include <Drivers/Interface/VUB/Stack.h>
#include <System/System.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/ResultStatus.h>
#include <array>
#include <atomic>
#include <cstddef>
#include <mutex>
#include <span>
#include <thread>
#include <type_traits>
#include <vector>

// Concurrent(): two application threads send through Stack::Send while the main thread runs Process. Send copies the payload into the
// staging ring under a critical section; Process moves the staged packets on, outside any section. The sections are a recursive mutex
// here, so they really exclude each other: the desktop shim of System only counts them. Every packet must arrive exactly once, and each
// thread's packets arrive in the order the thread sent them. The link and the receive handler check that they run outside any section.


// The critical sections of this program. Each enter locks the mutex, each leave unlocks it, on the thread that made the call. The depth
// is the number of sections the calling thread has open, so a handler or a link can tell whether it runs inside one.
std::recursive_mutex sectionMutex;
std::atomic<uint32> sectionEnters{0};
thread_local int sectionDepth = 0;


void System::CriticalSection(bool isEnable) {
	if (isEnable) {
		sectionMutex.lock();
		sectionDepth++;
		sectionEnters.fetch_add(1);
	} else {
		sectionDepth--;
		sectionMutex.unlock();
	}
}


inline constexpr std::size_t PerProducer = 150;
inline constexpr std::size_t Producers = 2;


// The packets the link took: each is two bytes, the producer and the number of the packet. seen counts each pair, so a packet taken twice or
// not at all shows. lastNumber is the number of the last packet of each producer, so an order error shows.
inline std::array<std::array<uint32, PerProducer>, Producers> seen{};
inline std::array<int, Producers> lastNumber{};
inline uint32 sinkPackets = 0;
inline uint32 sinkCorrupt = 0;
inline uint32 sinkOrderErrors = 0;
inline uint32 sinkDepthErrors = 0;
inline uint32 handlerCalls = 0;
inline uint32 handlerDepthErrors = 0;


// The link of the program: it takes every packet at once. Process is the only caller, so it needs no lock of its own.
struct SinkLink {
	static constexpr VUB::LinkTraits traits = VUB::LinkTraits().Framed().MaxPacket(64).FullDuplex();


	ResultStatus Send(std::span<const uint8> packet) {
		sinkPackets++;
		if (sectionDepth != 0) {
			sinkDepthErrors++;
		}

		if (packet.size() != 2 || packet[0] >= Producers || packet[1] >= PerProducer) {
			sinkCorrupt++;
			return ResultStatus::ok;
		}

		if (static_cast<int>(packet[1]) <= lastNumber[packet[0]]) {
			sinkOrderErrors++;
		}

		lastNumber[packet[0]] = static_cast<int>(packet[1]);
		seen[packet[0]][packet[1]]++;
		return ResultStatus::ok;
	}
};


// The receive handler. It runs from Process, so it must find no section open on this thread.
inline void SinkReceive(const VUB::Meta&, std::span<const uint8>) {
	handlerCalls++;
	if (sectionDepth != 0) {
		handlerDepthErrors++;
	}
}


inline uint32 SinkClock() {
	return 0;
}


// Options with the concurrent flag: the stack gives a layer that has this member the chain's Concurrent() value, and the layer keeps it as
// a constant (Configured), so it can make its own shared state critical only when the stack is concurrent.
struct GuardOptions {
	bool concurrent = false;
};


template<GuardOptions Value = GuardOptions{}>
struct GuardLayer {
	using Options = GuardOptions;


	template<Options value>
	using Configured = GuardLayer<value>;


	template<typename Builder>
	struct Step : Builder {
		explicit constexpr Step(const Builder& base) : Builder(base) {
		}
	};


	static constexpr bool concurrent = Value.concurrent;
	static constexpr std::size_t header = 0;
	static constexpr uint32 fingerprint = 0x47554152;
	static constexpr bool usesTime = false;
	static constexpr bool makesBulk = false;


	Result<std::size_t> Add(const VUB::Meta&, std::span<const uint8> in, std::span<uint8> out) {
		if (out.size() < in.size()) {
			return ResultStatus::invalidParameter;
		}

		for (std::size_t index = 0; index < in.size(); index++) {
			out[index] = in[index];
		}

		return in.size();
	}


	Result<VUB::Taken> Take(const VUB::Meta&, std::span<const uint8>) {
		return VUB::Taken{0, true};
	}


	void Process(uint32, const VUB::Outbox&) {
	}
};


using ConcurrentChain = VUB::Stack<+[] {
	return VUB::Builder()
		.CountDrops()
		.Layer<GuardLayer<>>()
		.Link<SinkLink>()
		.MaxPayload(8)
		.OnReceive<&SinkReceive>()
		.Clock<&SinkClock>()
		.Concurrent()
		.StageQueue(4);
}>;


// The same chain with a staging ring of one packet, so a second send before Process is refused.
using SmallChain = VUB::Stack<+[] {
	return VUB::Builder()
		.CountDrops()
		.Layer<GuardLayer<>>()
		.Link<SinkLink>()
		.MaxPayload(8)
		.OnReceive<&SinkReceive>()
		.Clock<&SinkClock>()
		.Concurrent()
		.StageQueue(1);
}>;


// The same chain without Concurrent(): its layer sees the flag off, and its stack builds no section.
using PlainChain = VUB::Stack<+[] {
	return VUB::Builder()
		.CountDrops()
		.Layer<GuardLayer<>>()
		.Link<SinkLink>()
		.MaxPayload(8)
		.OnReceive<&SinkReceive>()
		.Clock<&SinkClock>();
}>;


// Clears every counter of the program before a test.
void ResetCounters() {
	seen = {};
	lastNumber.fill(-1);
	sinkPackets = 0;
	sinkCorrupt = 0;
	sinkOrderErrors = 0;
	sinkDepthErrors = 0;
	handlerCalls = 0;
	handlerDepthErrors = 0;
	sectionEnters.store(0);
}


// A producer sends its packets with Stack::Send, and retries while the staging ring is full.
template<typename Chain>
void Produce(Chain& stack, uint8 producer) {
	for (std::size_t number = 0; number < PerProducer; number++) {
		std::array<uint8, 2> packet = {producer, static_cast<uint8>(number)};
		while (stack.Send(std::span<const uint8>(packet.data(), packet.size())) != ResultStatus::ok) {
			std::this_thread::yield();
		}
	}
}


// Two producers and the main thread's Process run at once; every packet arrives once, in each producer's order, and the link and the
// handler run with no section open.
void TestTwoProducersWhileProcessRuns() {
	ResetCounters();
	SinkLink link;
	ConcurrentChain stack(link);
	std::vector<std::thread> threads;
	for (std::size_t producer = 0; producer < Producers; producer++) {
		threads.emplace_back([&stack, producer]() {
			Produce(stack, static_cast<uint8>(producer));
		});
	}

	uint64 rounds = 0;
	while (sinkPackets < Producers * PerProducer && rounds < 50000000) {
		stack.Process();
		rounds++;
	}

	for (std::thread& thread : threads) {
		thread.join();
	}

	for (int drain = 0; drain < 100; drain++) {
		stack.Process();
	}

	SIM_EXPECT(sinkPackets == Producers * PerProducer);
	SIM_EXPECT(sinkCorrupt == 0);
	SIM_EXPECT(sinkOrderErrors == 0);
	SIM_EXPECT(sinkDepthErrors == 0);
	for (std::size_t producer = 0; producer < Producers; producer++) {
		for (std::size_t number = 0; number < PerProducer; number++) {
			SIM_EXPECT(seen[producer][number] == 1);
		}
	}

	SIM_EXPECT(sectionEnters.load() > 0);
}


// A packet taken from the receive queue goes to the handler from Process, with no section open.
void TestReceiveHandlerRunsOutsideSection() {
	ResetCounters();
	SinkLink link;
	ConcurrentChain stack(link);
	std::array<uint8, 2> packet = {0, 0};
	stack.FeedPacket(std::span<const uint8>(packet.data(), packet.size()));
	stack.Process();
	SIM_EXPECT(handlerCalls == 1);
	SIM_EXPECT(handlerDepthErrors == 0);
}


// A staging ring of one packet holds one payload: the second send is busy until Process has moved the first on.
void TestStageQueueRefusesWhenFull() {
	ResetCounters();
	SinkLink link;
	SmallChain stack(link);
	std::array<uint8, 2> first = {0, 1};
	std::array<uint8, 2> second = {0, 2};
	SIM_EXPECT(stack.Send(std::span<const uint8>(first.data(), first.size())) == ResultStatus::ok);
	SIM_EXPECT(stack.Send(std::span<const uint8>(second.data(), second.size())) == ResultStatus::busy);
	SIM_EXPECT(sinkPackets == 0);
	stack.Process();
	SIM_EXPECT(sinkPackets == 1);
	SIM_EXPECT(stack.Send(std::span<const uint8>(second.data(), second.size())) == ResultStatus::ok);
	stack.Process();
	SIM_EXPECT(sinkPackets == 2);
	SIM_EXPECT(seen[0][1] == 1);
	SIM_EXPECT(seen[0][2] == 1);
	SIM_EXPECT(sinkOrderErrors == 0);
}


// A plain chain sends its packets from the control queue in Process, with no section at all.
void TestPlainChainHasNoSection() {
	ResetCounters();
	SinkLink link;
	PlainChain stack(link);
	static_assert(!std::remove_cvref_t<decltype(stack.Find<GuardLayer<>>())>::concurrent);
	std::array<uint8, 2> packet = {0, 0};
	SIM_EXPECT(stack.Send(std::span<const uint8>(packet.data(), packet.size())) == ResultStatus::ok);
	stack.Process();
	SIM_EXPECT(sectionEnters.load() == 0);
	SIM_EXPECT(sinkPackets == 1);
}


int main() {
	TestTwoProducersWhileProcessRuns();
	TestReceiveHandlerRunsOutsideSection();
	TestStageQueueRefusesWhenFull();
	TestPlainChainHasNoSection();
	return SimFinish("TestVubStackConcurrent");
}
