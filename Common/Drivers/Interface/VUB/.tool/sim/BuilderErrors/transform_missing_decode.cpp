// expect: VubTransform
#include "SnippetSupport.h"


struct NoDecode {
	static constexpr std::size_t growth = 0;
	static constexpr uint32 fingerprint = 1;


	Result<std::size_t> Encode(const VubMeta&, std::span<const uint8> in, std::span<uint8> out) {
		(void)out;
		return in.size();
	}
};


using Stack = Vub<+[] {
	return VubBuilder()
		.Layer<TestLayer>()
		.Transform<NoDecode>()
		.Link<VubPacketEnd<64, 16>>()
		.MaxPayload(40)
		.OnReceive<&SnippetReceive>()
		.Clock<&SnippetClock>();
}>;


static_assert(Stack::count == 3);
