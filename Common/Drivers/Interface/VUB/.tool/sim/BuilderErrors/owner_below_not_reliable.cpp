// expect: must take its owner
#include "SnippetSupport.h"
#include <Drivers/Interface/VUB/Layer/Reliable.h>


// A layer that asks for its owner to be given to the block below it. The block below is a plain layer, which cannot take an owner: the
// stack refuses it when it is built.
struct OwnerLayer {
	static constexpr std::size_t header = 0;
	static constexpr uint32 fingerprint = 0x4F574E33;
	static constexpr bool usesTime = false;
	static constexpr bool makesBulk = false;
	static constexpr bool wantsOwner = true;


	static void DeliveryOutcome(const VUB::Outcome&) {
	}


	static void DeliveryRestart(void*) {
	}


	Result<std::size_t> Add(const VUB::Meta&, std::span<const uint8> in, std::span<uint8> out) {
		(void)out;
		return in.size();
	}


	Result<VUB::Taken> Take(const VUB::Meta&, std::span<const uint8>) {
		return VUB::Taken{0, true};
	}


	void Process(uint32, const VUB::Outbox&) {
	}
};


struct PlainBelow {
	static constexpr std::size_t header = 0;
	static constexpr uint32 fingerprint = 0x504C4E42;
	static constexpr bool usesTime = false;
	static constexpr bool makesBulk = false;


	Result<std::size_t> Add(const VUB::Meta&, std::span<const uint8> in, std::span<uint8> out) {
		(void)out;
		return in.size();
	}


	Result<VUB::Taken> Take(const VUB::Meta&, std::span<const uint8>) {
		return VUB::Taken{0, true};
	}


	void Process(uint32, const VUB::Outbox&) {
	}
};


using Stack = VUB::Stack<+[] {
	return VUB::Builder()
		.Layer<OwnerLayer>()
		.Layer<PlainBelow>()
		.Link<VUB::PacketEnd<64, 512>>()
		.MaxPayload(8)
		.OnReceive<&SnippetReceive>()
		.Clock<&SnippetClock>();
}>;


inline void BuildStack(VUB::PacketEnd<64, 512>& link) {
	Stack stack(link);
	(void)stack;
}
