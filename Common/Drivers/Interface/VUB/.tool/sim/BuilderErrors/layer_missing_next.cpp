// expect: VubLayer
#include "SnippetSupport.h"


struct NoNextLayer {
	static constexpr std::size_t header = 0;
	static constexpr uint32 fingerprint = 2;
	static constexpr bool usesTime = false;
	static constexpr bool makesBulk = true;


	Result<std::size_t> Add(const VubMeta&, std::span<const uint8> in, std::span<uint8> out) {
		(void)out;
		return in.size();
	}


	Result<VubTake> Take(const VubMeta&, std::span<const uint8> packet) {
		return VubTake{0, packet.size() > 0};
	}


	void Process(uint32 nowUs, const VubOutbox& outbox) {
		(void)nowUs;
		(void)outbox;
	}
};


using Stack = Vub<+[] {
	return VubBuilder()
		.Layer<NoNextLayer>()
		.Link<VubPacketEnd<64, 16>>()
		.MaxPayload(40)
		.OnReceive<&SnippetReceive>()
		.Clock<&SnippetClock>();
}>;


static_assert(Stack::count == 2);
