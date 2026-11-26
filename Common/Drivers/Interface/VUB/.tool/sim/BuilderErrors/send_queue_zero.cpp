// expect: a packet link with no Ready() needs a send queue
#include "SnippetSupport.h"


// A layer that sends no bulk packets, so the message is the packet link's.
struct PlainSnippetLayer {
	static constexpr std::size_t header = 0;
	static constexpr uint32 fingerprint = 0x504C4E31;
	static constexpr bool usesTime = false;
	static constexpr bool makesBulk = false;


	Result<std::size_t> Add(const VUB::Meta&, std::span<const uint8> in, std::span<uint8> out) {
		for (std::size_t index = 0; index < in.size() && index < out.size(); index++) {
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


using Stack = VUB::Stack<+[] {
	return VUB::Builder()
		.Layer<PlainSnippetLayer>()
		.Link<VUB::PacketEnd<64, 16>>()
		.MaxPayload(40)
		.SendQueue(0)
		.OnReceive<&SnippetReceive>()
		.Clock<&SnippetClock>();
}>;


static_assert(Stack::count == 2);
