// expect: VUB::FramingBlock
#include "SnippetSupport.h"


struct NoGrowthFraming {
	static constexpr bool usesTime = false;


	Result<std::size_t> Frame(const VUB::Meta&, std::span<const uint8> in, std::span<uint8> out) {
		(void)out;
		return in.size();
	}


	VUB::FrameStatus Push(uint8 byte, std::span<uint8> out, std::size_t& length) {
		(void)byte;
		(void)out;
		(void)length;
		return VUB::FrameStatus::pending;
	}
};


using Stack = VUB::Stack<+[] {
	return VUB::Builder()
		.Layer<TestLayer>()
		.Framing<NoGrowthFraming>()
		.Link<VUB::StreamEnd<64, 16>>()
		.MaxPayload(40)
		.OnReceive<&SnippetReceive>()
		.Clock<&SnippetClock>();
}>;


static_assert(Stack::count == 3);
