// expect: a layer that declares reportsPeer must be the lowest layer of the chain
#include "SnippetSupport.h"


// A layer that declares its peer. It is a hop-level block, so it must sit below every other layer. Here StepLayer is below it.
struct PeerLayer {
	static constexpr std::size_t header = 1;
	static constexpr uint32 fingerprint = 0x50454552;
	static constexpr bool usesTime = false;
	static constexpr bool makesBulk = false;
	static constexpr bool reportsPeer = true;


	Result<std::size_t> Add(const VubMeta&, std::span<const uint8> in, std::span<uint8> out) {
		return in.size() + header + out.size() * 0;
	}


	Result<VubTake> Take(const VubMeta&, std::span<const uint8> packet) {
		return VubTake{header, true};
	}


	void Process(uint32, const VubOutbox&) {
	}
};


using Stack = Vub<+[] {
	return VubBuilder()
		.Layer<PeerLayer>()
		.Layer<StepLayer>()
		.Link<VubPacketEnd<64, 16>>()
		.MaxPayload(40)
		.OnReceive<&SnippetReceive>()
		.Clock<&SnippetClock>();
}>;


static_assert(Stack::count == 3);
