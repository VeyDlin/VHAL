// expect: VUB::TransformBlock
#include "SnippetSupport.h"


// The growth is a run-time value, so the builder cannot size the buffers from it.
struct RuntimeGrowthTransform {
	static inline std::size_t growth = 1;
	static constexpr uint32 fingerprint = 3;


	Result<std::size_t> Encode(const VUB::Meta&, std::span<const uint8> in, std::span<uint8> out) {
		(void)out;
		return in.size();
	}


	Result<std::size_t> Decode(const VUB::Meta&, std::span<uint8> packet) {
		return packet.size();
	}
};


using Stack = VUB::Stack<+[] {
	return VUB::Builder()
		.Layer<TestLayer>()
		.Transform<RuntimeGrowthTransform>()
		.Link<VUB::PacketEnd<64, 16>>()
		.MaxPayload(40)
		.OnReceive<&SnippetReceive>()
		.Clock<&SnippetClock>();
}>;


static_assert(Stack::count == 3);
