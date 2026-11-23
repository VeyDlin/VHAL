// expect: VubFraming
#include "SnippetSupport.h"


struct NoGrowthFraming {
	static constexpr bool usesTime = false;


	Result<std::size_t> Frame(const VubMeta&, std::span<const uint8> in, std::span<uint8> out) {
		(void)out;
		return in.size();
	}


	VubFrame Push(uint8 byte, std::span<uint8> out, std::size_t& length) {
		(void)byte;
		(void)out;
		(void)length;
		return VubFrame::pending;
	}
};


using Stack = Vub<+[] {
	return VubBuilder()
		.Layer<TestLayer>()
		.Framing<NoGrowthFraming>()
		.Link<VubStreamEnd<64, 16>>()
		.MaxPayload(40)
		.OnReceive<&SnippetReceive>()
		.Clock<&SnippetClock>();
}>;


static_assert(Stack::count == 3);
