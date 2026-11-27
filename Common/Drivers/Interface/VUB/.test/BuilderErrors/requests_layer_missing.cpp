// expect: the chain has no requests layer
#include "SnippetSupport.h"


// A chain with no layer that declares requestsLayer: Requests() has no handle to give.
struct PlainHandleLayer {
	static constexpr std::size_t header = 0;
	static constexpr uint32 fingerprint = 0x504C4E31;
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
		.Layer<PlainHandleLayer>()
		.Link<VUB::PacketEnd<64, 16>>()
		.MaxPayload(40)
		.OnReceive<&SnippetReceive>()
		.Clock<&SnippetClock>();
}>;


inline void UseRequests(Stack& stack) {
	auto handle = stack.Requests();
	(void)handle;
}
