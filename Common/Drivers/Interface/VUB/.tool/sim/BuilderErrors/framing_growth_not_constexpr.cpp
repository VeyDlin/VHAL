// expect: VUB::FramingBlock
#include "SnippetSupport.h"


// Growth(length) is a run-time function, so the builder cannot size the framed buffer from it.
struct RuntimeGrowthFraming {
	static constexpr bool usesTime = false;


	static std::size_t Growth(std::size_t length) {
		return length + 6;
	}


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
		.Framing<RuntimeGrowthFraming>()
		.Link<VUB::StreamEnd<64, 16>>()
		.MaxPayload(40)
		.OnReceive<&SnippetReceive>()
		.Clock<&SnippetClock>();
}>;


static_assert(Stack::count == 3);
