#include "SimCheck.h"
#include "ReliableChains.h"
#include <Drivers/Interface/VUB/Layer/Reliable.h>
#include <Drivers/Interface/VUB/Meta.h>
#include <Drivers/Interface/VUB/Outbox.h>
#include <Drivers/Interface/VUB/Stack.h>
#include <Drivers/Interface/VUB/TestKit/Loopback.h>
#include <Utilities/DataTypes.h>
#include <cstddef>
#include <optional>
#include <span>

// The application-side send of a layer (C1), the owner pointer of Reliable's outcomes and restarts (C2), and the restart hook with the owner
// (C3). The layers here are test layers; the expectations are written from the design, not from the code's output.


// The count of the packets the layer above the relay took in its Add. A send from the application through the relay must not reach it.
inline std::size_t testUpperAdds = 0;

// The owner pointer the outcome and the restart handlers got, and the restart count of the owner form on a Reliable with no owner layer.
inline void* testOwnerSeen = nullptr;
inline void* testOwnerlessRestartOwner = nullptr;
inline uint32 testOwnerlessRestarts = 0;


// A layer above the relay: one header byte, and it counts its Add calls.
struct UpperLayer {
	static constexpr std::size_t header = 1;
	static constexpr uint32 fingerprint = 0x55505052;
	static constexpr bool usesTime = false;
	static constexpr bool makesBulk = false;


	Result<std::size_t> Add(const VUB::Meta&, std::span<const uint8> in, std::span<uint8> out) {
		testUpperAdds++;
		if (out.size() < in.size() + header) {
			return ResultStatus::invalidParameter;
		}

		out[0] = 0xAB;
		for (std::size_t index = 0; index < in.size(); index++) {
			out[header + index] = in[index];
		}

		return in.size() + header;
	}


	Result<VUB::Taken> Take(const VUB::Meta&, std::span<const uint8> packet) {
		if (packet.empty()) {
			return ResultStatus::invalidData;
		}

		return VUB::Taken{1, true};
	}


	void Process(uint32, const VUB::Outbox&) {
	}
};


// The relay: it is the requests layer of its chain, so Requests() gives its handle. Its handle sends into the blocks below it.
struct RelayLayer {
	static constexpr std::size_t header = 1;
	static constexpr uint32 fingerprint = 0x52454C41;
	static constexpr bool usesTime = false;
	static constexpr bool makesBulk = false;
	static constexpr bool requestsLayer = true;


	struct Handle {
		Handle(RelayLayer& owner, VUB::Outbox sender) : layer(owner), outbox(sender) {
		}


		ResultStatus Send(std::span<const uint8> payload, const VUB::Meta& meta = VUB::Meta()) const {
			return outbox.Send(payload, meta);
		}


		RelayLayer& layer;
		VUB::Outbox outbox;
	};


	Result<std::size_t> Add(const VUB::Meta&, std::span<const uint8> in, std::span<uint8> out) {
		if (out.size() < in.size() + header) {
			return ResultStatus::invalidParameter;
		}

		out[0] = 0x7E;
		for (std::size_t index = 0; index < in.size(); index++) {
			out[header + index] = in[index];
		}

		return in.size() + header;
	}


	Result<VUB::Taken> Take(const VUB::Meta&, std::span<const uint8> packet) {
		if (packet.empty()) {
			return ResultStatus::invalidData;
		}

		return VUB::Taken{1, true};
	}


	void Process(uint32, const VUB::Outbox&) {
	}
};


// The owner layer: it sits directly above Reliable and takes the outcomes and the restarts with its owner pointer. It has no header, so its
// packets are the bodies Reliable carries. Its handle is the requests handle of the chain.
struct OwnerLayer {
	static constexpr std::size_t header = 0;
	static constexpr uint32 fingerprint = 0x4F574E52;
	static constexpr bool usesTime = false;
	static constexpr bool makesBulk = false;
	static constexpr bool wantsOwner = true;
	static constexpr bool requestsLayer = true;


	struct Handle {
		Handle(OwnerLayer& owner, VUB::Outbox sender) : layer(owner), outbox(sender) {
		}


		ResultStatus Send(std::span<const uint8> payload, const VUB::Meta& meta = VUB::Meta()) const {
			return outbox.Send(payload, meta);
		}


		OwnerLayer& layer;
		VUB::Outbox outbox;
	};


	// The outcome of a packet sent through this layer. The owner pointer is this layer, given by the stack.
	static void DeliveryOutcome(const VUB::Outcome& outcome) {
		OwnerLayer* self = static_cast<OwnerLayer*>(outcome.owner);
		testOwnerSeen = outcome.owner;
		if (outcome.status == ResultStatus::ok) {
			self->okCount++;
		} else if (outcome.status == ResultStatus::reset) {
			self->resetCount++;
		}
	}


	static void DeliveryRestart(void* owner) {
		OwnerLayer* self = static_cast<OwnerLayer*>(owner);
		testOwnerSeen = owner;
		self->restarts++;
	}


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


	uint32 okCount = 0;
	uint32 resetCount = 0;
	uint32 restarts = 0;
};


// A chain with an application-side relay over a packet link, with no Reliable.
using RelayChain = VUB::Stack<+[] {
	return VUB::Builder()
		.Layer<UpperLayer>()
		.Layer<RelayLayer>()
		.Link<PacketEnd>()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


// The same with a send queue of one packet, so a second send while the first waits is refused.
using TinyRelayChain = VUB::Stack<+[] {
	return VUB::Builder()
		.Layer<UpperLayer>()
		.Layer<RelayLayer>()
		.Link<PacketEnd>()
		.MaxPayload(40)
		.SendQueue(1)
		.ReceiveQueue(4)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


// The owner chain: the owner layer above Reliable, over a reporting packet line, as the Reliable chains of the delivery tests.
using OwnerChain = VUB::Stack<+[] {
	return VUB::Builder()
		.Layer<OwnerLayer>()
		.Layer<ReliableLayer>().Retries(3).TimeoutMs(200).Window(4).PayloadBytes(40)
		.Link<ReportingPacketEnd>()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.OnReceive<&ReliableReceive>()
		.Clock<&TestClock>();
}>;


// Reliable with the restart hook that takes the owner, and no owner layer above it: the owner is null.
void OwnerlessRestart(void* owner) {
	testOwnerlessRestartOwner = owner;
	testOwnerlessRestarts++;
}


using OwnerlessChain = VUB::Stack<+[] {
	return VUB::Builder()
		.Layer<ReliableLayer>().Retries(3).TimeoutMs(200).Window(4).PayloadBytes(40).OnRestartOwner<&OwnerlessRestart>()
		.Link<ReportingPacketEnd>()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.OnReceive<&ReliableReceive>()
		.Clock<&TestClock>();
}>;


// Chain with a layer that declares largestPacket (C4): its packets are 40 bytes, and Reliable holds a payload of 40.
struct LargestLayer {
	static constexpr std::size_t header = 0;
	static constexpr std::size_t largestPacket = 40;
	static constexpr uint32 fingerprint = 0x4C415247;
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


using FitChain = VUB::Stack<+[] {
	return VUB::Builder()
		.Layer<LargestLayer>()
		.Layer<ReliableLayer>().Retries(3).TimeoutMs(200).Window(4).PayloadBytes(40).OnRestart<&ReliableRestart>()
		.Link<ReportingPacketEnd>()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.OnReceive<&ReliableReceive>()
		.Clock<&TestClock>();
}>;


// The application sends through the relay's handle: the packet enters the blocks below the relay and goes out as the link takes it. The
// layer above the relay is not run for it, so its Add is never called. Requests() and HandleAt<1>() give the same handle.
void TestHandleEntersBelowTheLayer() {
	ResetTestState();
	testUpperAdds = 0;
	PacketLine line(VUB::Faults{});
	RelayChain near(line.a);
	std::array<uint8, 3> payload = {1, 2, 3};
	RelayLayer::Handle byIndex = near.HandleAt<1>();
	SIM_EXPECT(byIndex.Send(std::span<const uint8>(payload.data(), payload.size())) == ResultStatus::ok);
	RelayLayer::Handle requests = near.Requests();
	std::array<uint8, 2> second = {9, 8};
	SIM_EXPECT(requests.Send(std::span<const uint8>(second.data(), second.size())) == ResultStatus::ok);
	near.Process();
	ReliableCapture capture;
	line.DeliverToB(capture);
	SIM_EXPECT(capture.count == 2);
	SIM_EXPECT(CaptureIs(capture, 0, std::array<uint8, 3>{1, 2, 3}));
	SIM_EXPECT(CaptureIs(capture, 1, std::array<uint8, 2>{9, 8}));
	SIM_EXPECT(testUpperAdds == 0);
	SIM_EXPECT(near.Drops().layerError == 0);
}


// A refusal of the application-side send goes back to the caller and is not counted: busy when the control queue is full, and
// messageTooLong when the packet is larger than the chain's packet.
void TestHandleRefusalsAreNotCounted() {
	ResetTestState();
	PacketLine line(VUB::Faults{});
	TinyRelayChain near(line.a);
	RelayLayer::Handle requests = near.Requests();
	std::array<uint8, 1> first = {1};
	std::array<uint8, 60> large{};
	SIM_EXPECT(requests.Send(std::span<const uint8>(first.data(), first.size())) == ResultStatus::ok);
	SIM_EXPECT(requests.Send(std::span<const uint8>(first.data(), first.size())) == ResultStatus::busy);
	SIM_EXPECT(requests.Send(std::span<const uint8>(large.data(), large.size())) == ResultStatus::messageTooLong);
	VUB::DropCounters drops = near.Drops();
	SIM_EXPECT(drops.layerError == 0);
	SIM_EXPECT(drops.sendError == 0);
	SIM_EXPECT(drops.queueFull == 0);
	near.Process();
	SIM_EXPECT(near.Requests().Send(std::span<const uint8>(first.data(), first.size())) == ResultStatus::ok);
}


// The owner layer gets each outcome with its owner pointer, which is the layer itself. Packets are sent through its handle, acknowledged
// by the far stack, and each outcome is counted once.
template<typename Near, typename Far>
void OwnerRound(ReportingPacketLine& line, Near& near, Far& far) {
	near.Process();
	line.DeliverToB(far);
	far.Process();
	line.DeliverToA(near);
	near.Process();
}


template<typename Near>
ResultStatus OwnerSend(Near& near, uint16 id) {
	std::array<uint8, 8> bytes = ReliablePayload(id);
	return near.Requests().Send(std::span<const uint8>(bytes.data(), bytes.size()));
}


template<typename Near, typename Far>
bool OwnerSendWhenReady(ReportingPacketLine& line, Near& near, Far& far, uint16 id, std::size_t maxRounds) {
	for (std::size_t round = 0; round < maxRounds; round++) {
		if (OwnerSend(near, id) == ResultStatus::ok) {
			return true;
		}

		OwnerRound(line, near, far);
	}

	return false;
}


void TestOwnerOutcomesAndRestartsReachTheOwnerLayer() {
	ResetReliableState();
	testOwnerSeen = nullptr;
	ReportingPacketLine line(VUB::Faults{});
	std::optional<OwnerChain> farStack;
	farStack.emplace(line.b);
	OwnerChain near(line.a);
	near.Find<ReliableLayer>().SetSession(ReliableSession);
	near.Process();
	farStack->Process();
	for (uint16 id = 0; id < 3; id++) {
		SIM_EXPECT(OwnerSendWhenReady(line, near, *farStack, id, 4));
		for (std::size_t round = 0; round < 3; round++) {
			OwnerRound(line, near, *farStack);
		}
	}

	SIM_EXPECT(near.Find<OwnerLayer>().okCount == 3);
	SIM_EXPECT(near.Find<OwnerLayer>().restarts == 0);
	SIM_EXPECT(testOwnerSeen == static_cast<void*>(&near.Find<OwnerLayer>()));
	farStack.reset();
	farStack.emplace(line.b);
	farStack->Process();
	SIM_EXPECT(OwnerSend(near, 3) == ResultStatus::ok);
	for (std::size_t round = 0; round < 6; round++) {
		OwnerRound(line, near, *farStack);
	}

	SIM_EXPECT(OwnerSendWhenReady(line, near, *farStack, 4, 6));
	for (std::size_t round = 0; round < 6; round++) {
		OwnerRound(line, near, *farStack);
	}

	// The packet sent right after the far stack rebooted is reset; the restart is reported once, to the owner, and the next one is ok.
	SIM_EXPECT(near.Find<OwnerLayer>().restarts == 1);
	SIM_EXPECT(near.Find<OwnerLayer>().resetCount == 1);
	SIM_EXPECT(near.Find<OwnerLayer>().okCount == 4);
	SIM_EXPECT(testOwnerSeen == static_cast<void*>(&near.Find<OwnerLayer>()));
	SIM_EXPECT(testDelivered[3] == 0);
	SIM_EXPECT(testDelivered[4] == 1);
}


// One round of the reporting packet line, as the delivery tests run it.
template<typename Near, typename Far>
void Round(ReportingPacketLine& line, Near& near, Far& far) {
	near.Process();
	line.DeliverToB(far);
	far.Process();
	line.DeliverToA(near);
	near.Process();
}


// Sends the payload of id as soon as the layer takes it, running rounds between the tries. Returns true when it was taken.
template<typename Near, typename Far>
bool SendWhenReady(ReportingPacketLine& line, Near& near, Far& far, uint16 id, std::size_t maxRounds) {
	for (std::size_t round = 0; round < maxRounds; round++) {
		if (ReliableSend(near, id) == ResultStatus::ok) {
			return true;
		}

		Round(line, near, far);
	}

	return false;
}


// The restart hook with the owner form (C3) on a Reliable with no owner layer: the owner is null, and the hook runs once per restart.
void TestRestartOwnerFormWithoutOwnerLayer() {
	ResetReliableState();
	testOwnerlessRestarts = 0;
	testOwnerlessRestartOwner = static_cast<void*>(&testOwnerlessRestarts);
	ReportingPacketLine line(VUB::Faults{});
	std::optional<OwnerlessChain> farStack;
	farStack.emplace(line.b);
	OwnerlessChain near(line.a);
	near.Find<ReliableLayer>().SetSession(ReliableSession);
	near.Process();
	farStack->Process();
	for (uint16 id = 0; id < 3; id++) {
		SIM_EXPECT(SendWhenReady(line, near, *farStack, id, 4));
		for (std::size_t round = 0; round < 3; round++) {
			Round(line, near, *farStack);
		}
	}

	SIM_EXPECT(testOwnerlessRestarts == 0);
	farStack.reset();
	farStack.emplace(line.b);
	farStack->Process();
	SIM_EXPECT(ReliableSend(near, 3) == ResultStatus::ok);
	for (std::size_t round = 0; round < 6; round++) {
		Round(line, near, *farStack);
	}

	SIM_EXPECT(SendWhenReady(line, near, *farStack, 4, 6));
	for (std::size_t round = 0; round < 6; round++) {
		Round(line, near, *farStack);
	}

	SIM_EXPECT(testOwnerlessRestarts == 1);
	SIM_EXPECT(testOwnerlessRestartOwner == nullptr);
}


// A layer above Reliable that declares largestPacket (C4) builds: its 40-byte packets fit Reliable's payload capacity of 40.
void TestLargestPacketFitsTheCapacity() {
	ResetReliableState();
	ReportingPacketLine line(VUB::Faults{});
	FitChain near(line.a);
	SIM_EXPECT(ReliableLayer::payloadCapacity == 40);
	SIM_EXPECT(LargestLayer::largestPacket == 40);
	(void)near;
}


int main() {
	TestHandleEntersBelowTheLayer();
	TestHandleRefusalsAreNotCounted();
	TestOwnerOutcomesAndRestartsReachTheOwnerLayer();
	TestRestartOwnerFormWithoutOwnerLayer();
	TestLargestPacketFitsTheCapacity();
	return SimFinish("TestVubStackLayers");
}
