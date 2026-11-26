// expect: remove the handler
#include "SnippetSupport.h"
#include <Drivers/Interface/VUB/Layer/Reliable.h>


// The layer above Reliable takes the restarts; an OnRestart of the application on the same Reliable is refused.
void SnippetRestart() {
}


struct OwnerLayer {
	static constexpr std::size_t header = 0;
	static constexpr uint32 fingerprint = 0x4F574E32;
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


using Stack = VUB::Stack<+[] {
	return VUB::Builder()
		.Layer<OwnerLayer>()
		.Layer<VUB::ReliableOf<32, 4>>().Retries(3).OnRestart<&SnippetRestart>()
		.Link<VUB::PacketEnd<64, 512>>()
		.MaxPayload(40)
		.OnReceive<&SnippetReceive>()
		.Clock<&SnippetClock>();
}>;


static_assert(Stack::count == 3);
