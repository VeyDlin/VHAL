// libs-linux: pthread
#include "SimCheck.h"
#include <Drivers/Interface/VUB/LinkTraits.h>
#include <Drivers/Interface/VUB/Outbox.h>
#include <Drivers/Interface/VUB/Stack.h>
#include <System/System.h>
#include <Utilities/DataTypes.h>
#include <atomic>
#include <array>
#include <cstddef>
#include <mutex>
#include <span>
#include <thread>
#include <type_traits>
#include <vector>

// Concurrent() (C5): two application threads send through a layer's handle while the main thread runs Process. The stack's critical sections
// are a recursive mutex here, so the sections really exclude each other: the desktop shim of System only counts them. Every packet must
// arrive exactly once.


// The critical sections of this program. Each enter locks the mutex, each leave unlocks it, on the thread that made the call.
std::recursive_mutex sectionMutex;
std::atomic<uint32> sectionEnters{0};


void System::CriticalSection(bool isEnable) {
	if (isEnable) {
		sectionMutex.lock();
		sectionEnters.fetch_add(1);
	} else {
		sectionMutex.unlock();
	}
}


inline constexpr std::size_t PerProducer = 150;
inline constexpr std::size_t Producers = 2;


// The packets the link took: each is two bytes, the producer and the number of the packet. seen counts each pair, so a packet taken twice or
// not at all shows.
inline std::array<std::array<uint32, PerProducer>, Producers> seen{};
inline uint32 sinkPackets = 0;
inline uint32 sinkCorrupt = 0;


// The link of the program: it takes every packet at once. Process is the only caller, so it needs no lock of its own.
struct SinkLink {
	static constexpr VUB::LinkTraits traits = VUB::LinkTraits().Framed().MaxPacket(64).FullDuplex();


	ResultStatus Send(std::span<const uint8> packet) {
		sinkPackets++;
		if (packet.size() != 2 || packet[0] >= Producers || packet[1] >= PerProducer) {
			sinkCorrupt++;
			return ResultStatus::ok;
		}

		seen[packet[0]][packet[1]]++;
		return ResultStatus::ok;
	}
};


inline void SinkReceive(const VUB::Meta&, std::span<const uint8>) {
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
	static constexpr bool requestsLayer = true;


	struct Handle {
		Handle(GuardLayer& owner, VUB::Outbox sender) : layer(owner), outbox(sender) {
		}


		ResultStatus Send(std::span<const uint8> payload) const {
			return outbox.Send(payload);
		}


		GuardLayer& layer;
		VUB::Outbox outbox;
	};


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
		.Layer<GuardLayer<>>()
		.Link<SinkLink>()
		.MaxPayload(8)
		.OnReceive<&SinkReceive>()
		.Clock<&SinkClock>()
		.Concurrent();
}>;


// The same chain without Concurrent(): its layer sees the flag off, and its stack builds no section.
using PlainChain = VUB::Stack<+[] {
	return VUB::Builder()
		.Layer<GuardLayer<>>()
		.Link<SinkLink>()
		.MaxPayload(8)
		.OnReceive<&SinkReceive>()
		.Clock<&SinkClock>();
}>;


// A producer sends its packets through its copy of the handle, and retries while the control queue is full.
template<typename Handle>
void Produce(Handle handle, uint8 producer) {
	for (std::size_t number = 0; number < PerProducer; number++) {
		std::array<uint8, 2> packet = {producer, static_cast<uint8>(number)};
		while (handle.Send(std::span<const uint8>(packet.data(), packet.size())) != ResultStatus::ok) {
			std::this_thread::yield();
		}
	}
}


// Two producers and the main thread's Process run at once; every packet arrives once.
void TestTwoProducersWhileProcessRuns() {
	seen = {};
	sinkPackets = 0;
	sinkCorrupt = 0;
	sectionEnters.store(0);
	SinkLink link;
	ConcurrentChain stack(link);
	static_assert(std::remove_cvref_t<decltype(stack.Find<GuardLayer<>>())>::concurrent);
	std::vector<std::thread> threads;
	for (std::size_t producer = 0; producer < Producers; producer++) {
		GuardLayer<GuardOptions{true}>::Handle handle = stack.Requests();
		threads.emplace_back([handle, producer]() {
			Produce(handle, static_cast<uint8>(producer));
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
	for (std::size_t producer = 0; producer < Producers; producer++) {
		for (std::size_t number = 0; number < PerProducer; number++) {
			SIM_EXPECT(seen[producer][number] == 1);
		}
	}

	SIM_EXPECT(sectionEnters.load() > 0);
}


// A plain chain builds its layer with the flag off, and sends with no section.
void TestPlainChainHasNoSection() {
	sectionEnters.store(0);
	uint32 before = sinkPackets;
	SinkLink link;
	PlainChain stack(link);
	static_assert(!std::remove_cvref_t<decltype(stack.Find<GuardLayer<>>())>::concurrent);
	std::array<uint8, 2> packet = {0, 0};
	SIM_EXPECT(stack.Requests().Send(std::span<const uint8>(packet.data(), packet.size())) == ResultStatus::ok);
	stack.Process();
	SIM_EXPECT(sectionEnters.load() == 0);
	SIM_EXPECT(sinkPackets == before + 1);
}


int main() {
	TestTwoProducersWhileProcessRuns();
	TestPlainChainHasNoSection();
	return SimFinish("TestVubStackConcurrent");
}
