// expect: VubFraming
#include "SnippetSupport.h"


// Growth(length) is a run-time function, so the builder cannot size the framed buffer from it.
struct RuntimeGrowthFraming {
	static constexpr bool usesTime = false;


	static std::size_t Growth(std::size_t length) {
		return length + 6;
	}


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
		.Framing<RuntimeGrowthFraming>()
		.Link<VubStreamEnd<64, 16>>()
		.MaxPayload(40)
		.OnReceive<&SnippetReceive>()
		.Clock<&SnippetClock>();
}>;


static_assert(Stack::count == 3);
