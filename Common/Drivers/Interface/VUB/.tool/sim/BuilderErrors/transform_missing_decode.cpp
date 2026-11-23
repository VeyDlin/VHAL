// expect: VUB::TransformBlock
#include "SnippetSupport.h"


struct NoDecode {
	static constexpr std::size_t growth = 0;
	static constexpr uint32 fingerprint = 1;


	Result<std::size_t> Encode(const VUB::Meta&, std::span<const uint8> in, std::span<uint8> out) {
		(void)out;
		return in.size();
	}
};


using Stack = VUB::Stack<+[] {
	return VUB::Builder()
		.Layer<TestLayer>()
		.Transform<NoDecode>()
		.Link<VUB::PacketEnd<64, 16>>()
		.MaxPayload(40)
		.OnReceive<&SnippetReceive>()
		.Clock<&SnippetClock>();
}>;


static_assert(Stack::count == 3);
