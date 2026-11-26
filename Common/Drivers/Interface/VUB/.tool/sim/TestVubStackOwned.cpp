#include "Chains.h"
#include "SimCheck.h"
#include <Drivers/Interface/VUB/Layer/Reliable.h>
#include <Drivers/Interface/VUB/Meta.h>
#include <Drivers/Interface/VUB/Outbox.h>
#include <Drivers/Interface/VUB/Stack.h>
#include <Drivers/Interface/VUB/TestKit/Loopback.h>
#include <Utilities/DataTypes.h>
#include <array>
#include <cstddef>
#include <span>

// A layer that is configured by a Step (so the stack stores another type for it than the one in the chain) and that declares wantsOwner, above
// the delivery layer. The outcomes must reach the stored layer: the owner pointer is the stored object, and the handlers are of its type.


struct OwnedOptions {
	uint8 pad = 0;
};


template<OwnedOptions Value = OwnedOptions{}>
class OwnedLayerOf {
public:
	using Options = OwnedOptions;

	template<OwnedOptions value>
	using Configured = OwnedLayerOf<value>;


	template<typename Builder>
	struct Step : Builder {
		explicit constexpr Step(const Builder& base) : Builder(base) {
		}


		constexpr Step Pad(uint8 value) const {
			Options next = this->LastOptions();
			next.pad = value;
			return Step(this->WithLastOptions(next));
		}
	};


	static constexpr std::size_t header = 0;
	static constexpr uint32 fingerprint = 0x4F574E31;
	static constexpr bool usesTime = false;
	static constexpr bool makesBulk = false;
	static constexpr bool wantsOwner = true;


	std::array<uint32, Value.pad> padArea{};
	uint32 okCount = 0;


	static void DeliveryOutcome(const VUB::Outcome& outcome) {
		OwnedLayerOf* self = static_cast<OwnedLayerOf*>(outcome.owner);
		if (outcome.status == ResultStatus::ok) {
			self->okCount++;
		}
	}


	static void DeliveryRestart(void*) {
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
};


struct OptionalOwnerOptions {
	uint8 pad = 0;
	bool ownerBelow = false;
};


// A layer that works with and without the owner of its outcomes: the builder tells it through ownerBelow what is below it.
template<OptionalOwnerOptions Value = OptionalOwnerOptions{}>
class OptionalOwnerLayerOf {
public:
	using Options = OptionalOwnerOptions;

	template<OptionalOwnerOptions value>
	using Configured = OptionalOwnerLayerOf<value>;


	template<typename Builder>
	struct Step : Builder {
		explicit constexpr Step(const Builder& base) : Builder(base) {
		}


		constexpr Step Pad(uint8 value) const {
			Options next = this->LastOptions();
			next.pad = value;
			return Step(this->WithLastOptions(next));
		}
	};


	static constexpr std::size_t header = 0;
	static constexpr uint32 fingerprint = 0x4F574E32;
	static constexpr bool usesTime = false;
	static constexpr bool makesBulk = false;
	static constexpr bool wantsOwner = true;
	static constexpr bool ownerOptional = true;
	static constexpr bool hasOwner = Value.ownerBelow;
	static constexpr uint8 padValue = Value.pad;


	uint32 okCount = 0;


	static void DeliveryOutcome(const VUB::Outcome& outcome) {
		OptionalOwnerLayerOf* self = static_cast<OptionalOwnerLayerOf*>(outcome.owner);
		if (outcome.status == ResultStatus::ok) {
			self->okCount++;
		}
	}


	static void DeliveryRestart(void*) {
	}


	bool HasOwner() const {
		return hasOwner;
	}


	uint8 Pad() const {
		return padValue;
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
};


// The same shape with a Handle: the application reaches the layer through the stack.
struct HandledLayer {
	static constexpr std::size_t header = 0;
	static constexpr uint32 fingerprint = 0x48414E44;
	static constexpr bool usesTime = false;
	static constexpr bool makesBulk = false;
	static constexpr bool requestsLayer = true;


	class Handle {
	public:
		Handle(HandledLayer& layerObject, VUB::Outbox outboxObject) : layer(&layerObject), outbox(outboxObject) {
		}


		HandledLayer& Layer() const {
			return *layer;
		}


		ResultStatus Send(std::span<const uint8> payload) const {
			return outbox.Send(payload, VUB::Meta());
		}


	private:
		HandledLayer* layer;
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


using OwnedStack = VUB::Stack<+[] {
	return VUB::Builder()
		.Layer<OwnedLayerOf<>>().Pad(4)
		.Layer<VUB::ReliableOf<40, 4>>().Retries(3).TimeoutMs(200).Window(4)
		.Link<ReportingPacketEnd>()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


using OwnedDefaultStack = VUB::Stack<+[] {
	return VUB::Builder()
		.Layer<OwnedLayerOf<>>()
		.Layer<VUB::ReliableOf<40, 4>>().Retries(3).TimeoutMs(200).Window(4)
		.Link<ReportingPacketEnd>()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


using OptionalOverReliable = VUB::Stack<+[] {
	return VUB::Builder()
		.Layer<OptionalOwnerLayerOf<>>().Pad(3)
		.Layer<VUB::ReliableOf<40, 4>>().Retries(3).TimeoutMs(200).Window(4)
		.Link<ReportingPacketEnd>()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


using OptionalOverLink = VUB::Stack<+[] {
	return VUB::Builder()
		.Layer<OptionalOwnerLayerOf<>>().Pad(3)
		.Link<ReportingPacketEnd>()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


using HandledStack = VUB::Stack<+[] {
	return VUB::Builder()
		.Layer<HandledLayer>()
		.Link<ReportingPacketEnd>()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


using HandledConcurrentStack = VUB::Stack<+[] {
	return VUB::Builder()
		.Layer<HandledLayer>()
		.Link<ReportingPacketEnd>()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.Concurrent()
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


// Runs one delivered packet over two stacks, and checks that its outcome reached the stored owner layer and wrote nothing else there.
template<typename Stack>
void RunDelivered() {
	ReportingPacketLine line(VUB::Faults{});
	Stack near(line.a);
	Stack far(line.b);
	near.template Find<VUB::ReliableOf<40, 4>>().SetSession(1);
	far.template Find<VUB::ReliableOf<40, 4>>().SetSession(2);
	std::array<uint8, 4> payload = {1, 2, 3, 4};
	ResultStatus sent = ResultStatus::busy;
	for (int round = 0; round < 12; round++) {
		if (sent != ResultStatus::ok) {
			sent = near.Send(std::span<const uint8>(payload.data(), payload.size()));
		}

		near.Process();
		line.DeliverToB(far);
		far.Process();
		line.DeliverToA(near);
		near.Process();
	}

	SIM_EXPECT(sent == ResultStatus::ok);
	SIM_EXPECT(near.template Find<OwnedLayerOf<>>().okCount == 1);
	for (std::size_t index = 0; index < near.template Find<OwnedLayerOf<>>().padArea.size(); index++) {
		SIM_EXPECT(near.template Find<OwnedLayerOf<>>().padArea[index] == 0);
	}
}


// The handle of a stack that is not concurrent sends through the layers below; the handle of a concurrent one refuses.
void TestHandleSendsOnlyOutsideAConcurrentStack() {
	ReportingPacketLine line(VUB::Faults{});
	HandledStack plain(line.a);
	HandledConcurrentStack concurrentStack(line.b);
	std::array<uint8, 2> payload = {7, 8};
	SIM_EXPECT(&plain.Requests().Layer() == &plain.Find<HandledLayer>());
	SIM_EXPECT(plain.Requests().Send(payload) == ResultStatus::ok);
	SIM_EXPECT(&concurrentStack.Requests().Layer() == &concurrentStack.Find<HandledLayer>());
	SIM_EXPECT(concurrentStack.Requests().Send(payload) == ResultStatus::notSupported);
}


// A layer with ownerOptional gets its outcomes above Reliable, and builds above a block that takes no owner. It knows which one it is.
void TestOptionalOwnerFollowsTheBlockBelow() {
	ReportingPacketLine line(VUB::Faults{});
	OptionalOverReliable near(line.a);
	OptionalOverReliable far(line.b);
	near.Find<VUB::ReliableOf<40, 4>>().SetSession(1);
	far.Find<VUB::ReliableOf<40, 4>>().SetSession(2);
	std::array<uint8, 4> payload = {1, 2, 3, 4};
	ResultStatus sent = ResultStatus::busy;
	for (int round = 0; round < 12; round++) {
		if (sent != ResultStatus::ok) {
			sent = near.Send(std::span<const uint8>(payload.data(), payload.size()));
		}

		near.Process();
		line.DeliverToB(far);
		far.Process();
		line.DeliverToA(near);
		near.Process();
	}

	SIM_EXPECT(sent == ResultStatus::ok);
	SIM_EXPECT(near.Find<OptionalOwnerLayerOf<>>().HasOwner());
	SIM_EXPECT(near.Find<OptionalOwnerLayerOf<>>().Pad() == 3);
	SIM_EXPECT(near.Find<OptionalOwnerLayerOf<>>().okCount == 1);

	ReportingPacketLine plainLine(VUB::Faults{});
	OptionalOverLink plain(plainLine.a);
	SIM_EXPECT(!plain.Find<OptionalOwnerLayerOf<>>().HasOwner());
	SIM_EXPECT(plain.Find<OptionalOwnerLayerOf<>>().Pad() == 3);
	SIM_EXPECT(plain.Send(std::span<const uint8>(payload.data(), payload.size())) == ResultStatus::ok);
	plain.Process();
	SIM_EXPECT(plain.Find<OptionalOwnerLayerOf<>>().okCount == 0);
}


int main() {
	RunDelivered<OwnedDefaultStack>();
	RunDelivered<OwnedStack>();
	TestHandleSendsOnlyOutsideAConcurrentStack();
	TestOptionalOwnerFollowsTheBlockBelow();
	return SimFinish("TestVubStackOwned");
}
