// expect: VUB::LayerBlock
#include "SnippetSupport.h"


struct NoNextLayer {
	static constexpr std::size_t header = 0;
	static constexpr uint32 fingerprint = 2;
	static constexpr bool usesTime = false;
	static constexpr bool makesBulk = true;


	Result<std::size_t> Add(const VUB::Meta&, std::span<const uint8> in, std::span<uint8> out) {
		(void)out;
		return in.size();
	}


	Result<VUB::Taken> Take(const VUB::Meta&, std::span<const uint8> packet) {
		return VUB::Taken{0, packet.size() > 0};
	}


	void Process(uint32 nowUs, const VUB::Outbox& outbox) {
		(void)nowUs;
		(void)outbox;
	}
};


using Stack = VUB::Stack<+[] {
	return VUB::Builder()
		.Layer<NoNextLayer>()
		.Link<VUB::PacketEnd<64, 16>>()
		.MaxPayload(40)
		.OnReceive<&SnippetReceive>()
		.Clock<&SnippetClock>();
}>;


static_assert(Stack::count == 2);
